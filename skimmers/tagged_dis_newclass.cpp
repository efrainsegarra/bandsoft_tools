#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cmath>
#include <string>
#include <vector>

#include "TFile.h"
#include "TTree.h"
#include "TVector3.h"
#include "TH2.h"
#include "TH1.h"
#include "TClonesArray.h"

#include "reader.h"
#include "bank.h"

#include "BParticle.h"
#include "BCalorimeter.h"
#include "BScintillator.h"
#include "BBand.h"
#include "BEvent.h"

#include "RCDB/Connection.h"

#include "constants.h"
#include "readhipo_helper.h"
#include "e_pid.h"
#include "DC_fiducial.h"

#include "bandreco.h"

using namespace std;


int main(int argc, char** argv) {
	// check number of arguments
	if( argc < 5 ){
		cerr << "Incorrect number of arugments. Instead use:\n\t./code [outputFile] [MC/DATA] [load shifts] [inputFile] \n\n";
		cerr << "\t\t[outputFile] = ____.root\n";
		cerr << "\t\t[<MC,DATA, MC generated info for each event> = <0, 1, 2> \n";
		cerr << "\t\t[<load shifts N,Y> = <0, 1> \n";
		cerr << "\t\t[inputFile] = ____.hipo ____.hipo ____.hipo ...\n\n";
		return -1;
	}

	int MC_DATA_OPT = atoi(argv[2]);
	int loadshifts_opt = atoi(argv[3]);

	// Initialize our BAND reconstruction engine:
	BANDReco * BAND = new BANDReco();

	// Create output tree
	TFile * outFile = new TFile(argv[1],"RECREATE");
	TTree * outTree = new TTree("tagged","BAND Neutrons and CLAS Electrons");
	//	Event info:
	int Runno		= 0;
	double Ebeam		= 0;
	double gated_charge	= 0;
	double livetime		= 0;
	double starttime	= 0;
	double current		= 0;
	int eventnumber = 0;
	bool goodneutron = false;
	int nleadindex = -1;
	double weight		= 0;
	//	MC info:
	int genMult		= 0;
	TClonesArray * mcParts = new TClonesArray("genpart");
	TClonesArray &saveMC = *mcParts;
	// 	Neutron info:
	int nMult		= 0;
	TClonesArray * nHits = new TClonesArray("bandhit");
	TClonesArray &saveHit = *nHits;
	//	Electron info:
	clashit eHit;
	//	Tagged info:
	TClonesArray * tags = new TClonesArray("taghit");
	TClonesArray &saveTags = *tags;
	// 	Event branches:
	outTree->Branch("Runno"		,&Runno			);
	outTree->Branch("Ebeam"		,&Ebeam			);
	outTree->Branch("gated_charge"	,&gated_charge		);
	outTree->Branch("livetime"	,&livetime		);
	outTree->Branch("starttime"	,&starttime		);
	outTree->Branch("current"	,&current		);
	outTree->Branch("eventnumber",&eventnumber);
	outTree->Branch("weight"	,&weight		);
	//	Neutron branches:
	outTree->Branch("nMult"		,&nMult			);
	outTree->Branch("nHits"		,&nHits			);
	//Branches to store if good Neutron event and leadindex
	outTree->Branch("goodneutron"		,&goodneutron	);
	outTree->Branch("nleadindex"		,&nleadindex			);
	//	Electron branches:
	outTree->Branch("eHit"		,&eHit			);
	//	Tagged branches:
	outTree->Branch("tag"		,&tags			);
	//	MC branches:
	outTree->Branch("genMult"	,&genMult		);
	outTree->Branch("mcParts"	,&mcParts		);

	// Connect to the RCDB
	rcdb::Connection connection("mysql://rcdb@clasdb.jlab.org/rcdb");

	// Load the electron PID class:
	e_pid ePID;
	// Load the DC fiducial class for electrons;
	DCFiducial DCfid_electrons;


	// Load input file
	for( int i = 4 ; i < argc ; i++ ){
		if( MC_DATA_OPT == 0 || MC_DATA_OPT == 2){
			int runNum = 11;
			Runno = runNum;
		}
		else if( MC_DATA_OPT == 1){
			int runNum = getRunNumber(argv[i]);
			Runno = runNum;
			auto cnd = connection.GetCondition(runNum, "beam_energy");
			Ebeam = cnd->ToDouble() / 1000.; // [GeV]
			current = connection.GetCondition( runNum, "beam_current") ->ToDouble(); // [nA]
		}
		else{
			exit(-1);
		}
		//Set cut parameters for electron PID. This only has 10.2 and 10.6 implemented
		ePID.setParamsRGB(Ebeam);

		// Setup hipo reading for this file
		TString inputFile = argv[i];
		hipo::reader reader;
		reader.open(inputFile);
		hipo::dictionary  factory;
		hipo::schema	  schema;
		reader.readDictionary(factory);
		BEvent		event_info		(factory.getSchema("REC::Event"		));
		BBand		band_hits		(factory.getSchema("BAND::hits"		));
		hipo::bank	scaler			(factory.getSchema("RUN::scaler"	));
		hipo::bank  	run_config 		(factory.getSchema("RUN::config"));
		hipo::bank      DC_Track                (factory.getSchema("REC::Track"         ));
		hipo::bank      DC_Traj                 (factory.getSchema("REC::Traj"          ));
		hipo::event 	readevent;
		hipo::bank	band_rawhits		(factory.getSchema("BAND::rawhits"	));
		hipo::bank	band_adc		(factory.getSchema("BAND::adc"		));
		hipo::bank	band_tdc		(factory.getSchema("BAND::tdc"		));
		BParticle	particles		(factory.getSchema("REC::Particle"	));
		BCalorimeter	calorimeter		(factory.getSchema("REC::Calorimeter"	));
		BScintillator	scintillator		(factory.getSchema("REC::Scintillator"	));
		hipo::bank	mc_event_info		(factory.getSchema("MC::Event"		));
		hipo::bank	mc_particle		(factory.getSchema("MC::Particle"	));


		// Loop over all events in file
		int event_counter = 0;
		gated_charge = 0;
		livetime	= 0;
		double torussetting = 0;
		while(reader.next()==true){
			BAND->Clear();
			// Clear all branches
			gated_charge	= 0;
			livetime	= 0;
			starttime 	= 0;
			eventnumber = 0;
			// Neutron
			nMult		= 0;
			nleadindex = -1;
			goodneutron = false;
			bandhit nHit[maxNeutrons];
			nHits->Clear();
			// Tag
			taghit tag[maxNeutrons];
			tags->Clear();
			// Electron
			eHit.Clear();
			// MC
			genMult = 0;
			weight = 1;
			genpart mcPart[maxGens];
			mcParts->Clear();


			// Count events
			if(event_counter%10000==0) cout << "event: " << event_counter << endl;
			//if( event_counter > 30 ) break;
			event_counter++;

			// Load data structure for this event:
			reader.read(readevent);
			readevent.getStructure(event_info);
			readevent.getStructure(scaler);
			readevent.getStructure(run_config);
			// band struct
			readevent.getStructure(band_hits);
			readevent.getStructure(band_rawhits);
			readevent.getStructure(band_adc);
			readevent.getStructure(band_tdc);
			// electron struct
			readevent.getStructure(particles);
			readevent.getStructure(calorimeter);
			readevent.getStructure(scintillator);
			readevent.getStructure(DC_Track);
			readevent.getStructure(DC_Traj);
			// monte carlo struct
			readevent.getStructure(mc_event_info);
			readevent.getStructure(mc_particle);

			if( event_counter == 1 ){
				int period = -1;
				//Load of shifts depending on run number
				if (Runno >= 11286 && Runno < 11304)	{ //LER runs
				}
				else if (Runno > 6100 && Runno < 6800) { //Spring 19 data
					period = 0;
				}
				else if (Runno > 11320 && Runno < 11580) { //Spring 20 data
					period = 1;
				}		
				else {
					cout << "No bar by bar offsets loaded " << endl;
					cout << "Check shift option when starting program. Exit " << endl;
					exit(-1);
				}
				if( period == -1 ){ cerr << "invalid period\n"; exit(-1); }
				BAND->setPeriod(period);
				BAND->readTW();			// TW calibration values for each PMT
				BAND->readLROffset();		// (L-R) offsets for each bar
				BAND->readPaddleOffset();	// bar offsets relative to bar 2X7 in each layer X
				BAND->readLayerOffset();	// layer offsets relative to layer 5
				BAND->readGeometry();		// geometry table for each bar
				BAND->readEnergyCalib();	// energy calibration for Adc->MeVee 
				BAND->readStatus();		// status table for 0,1 = bad,good bar
				if( loadshifts_opt ) BAND->readGlobalOffset();	// final global alignment relative to electron trigger
			}



			//Get Event number from RUN::config
			eventnumber = run_config.getInt( 1 , 0 );

			//from first event get RUN::config torus Setting
		 	// inbending = negative torussetting, outbending = torusseting
			torussetting = run_config.getFloat( 7 , 0 );


			// For simulated events, get the weight for the event
			if( MC_DATA_OPT == 0 || MC_DATA_OPT == 2){
				getMcInfo( mc_particle , mc_event_info , mcPart , starttime, weight, Ebeam , genMult );
			}

			//for option 2 just fill the generated infos in the output file, skip remaining part of event loop
			if( MC_DATA_OPT == 2){
				// Store the mc particles in TClonesArray
				for( int n = 0 ; n < maxGens ; n++ ){
					new(saveMC[n]) genpart;
					saveMC[n] = &mcPart[n];
				}
				outTree->Fill();
				continue;
			}

				//Till the end of the for loop is only executed for MC_DATA_OPT = 0 and 1
			// Get integrated charge, livetime and start-time from REC::Event
			if( event_info.getRows() == 0 ) continue;
			getEventInfo( event_info, gated_charge, livetime, starttime );




			// Grab the electron information:
			getElectronInfo( particles , calorimeter , scintillator , DC_Track, DC_Traj, 0, eHit , starttime , Runno , Ebeam );

			//check electron PID in EC with Andrew's class
			if( !(ePID.isElectron(&eHit)) ) continue;


			//bending field of torus for DC fiducial class ( 1 = inbeding, 0 = outbending	)
			int bending;
			//picking up torussetting from RUN::config, inbending = negative torussetting, outbending = positive torusseting
			if (torussetting > 0 && torussetting <=1.0) { //outbending
				bending = 0;
			}
			else if (torussetting < 0 && torussetting >=-1.0) { //inbending
				bending = 1;
			}
			else {
				cout << "WARNING: Torus setting from RUN::config is " << torussetting << ". This is not defined for bending value for DC fiducials. Please check " << endl;
			}
			if (eHit.getDC_sector() == -999 || eHit.getDC_sector() == -1  ) {
				cout << "Skimmer Error: DC sector is  " << eHit.getDC_sector() << " . Skipping event "<< event_counter << endl;
				eHit.Print();
				continue;
			}

			//checking DC Fiducials
			//Region 1, true = pass DC Region 1
			bool DC_fid_1  = DCfid_electrons.DC_e_fid(eHit.getDC_x1(),eHit.getDC_y1(),eHit.getDC_sector(), 1, bending);
			//checking DC Fiducials
			//Region 2, true = pass DC Region 2
			bool DC_fid_2  = DCfid_electrons.DC_e_fid(eHit.getDC_x2(),eHit.getDC_y2(),eHit.getDC_sector(), 2, bending);
			//checking DC Fiducials
			//Region 3, true = pass DC Region 3
			bool DC_fid_3  = DCfid_electrons.DC_e_fid(eHit.getDC_x3(),eHit.getDC_y3(),eHit.getDC_sector(), 3, bending);

			//check if any of the fiducials is false i.e. electron does not pass all DC fiducials
			if (!DC_fid_1 || !DC_fid_2 || !DC_fid_3) continue;

			// Grab the neutron information:
				// Form the PMTs and Bars for BAND:
			BAND->createPMTs( &band_adc, &band_tdc, &run_config );
			BAND->createBars();
			BAND->storeHits( nMult , nHit , starttime , eHit.getVtz() ); // use event-by-event electron vertex for pathlength-z

			// Create the tagged information if we have neutrons appropriately aligned in time:
			getTaggedInfo(	eHit	,  nHit	 ,  tag  , Ebeam , nMult );

			// Store the neutrons in TClonesArray
			for( int n = 0 ; n < nMult ; n++ ){
				new(saveHit[n]) bandhit;
				saveHit[n] = &nHit[n];
			}
			for( int n = 0 ; n < nMult ; n++ ){
				new(saveTags[n]) taghit;
				saveTags[n] = &tag[n];
			}
			// Store the mc particles in TClonesArray
			for( int n = 0 ; n < maxGens ; n++ ){
				new(saveMC[n]) genpart;
				saveMC[n] = &mcPart[n];
			}

			if (nMult == 1) {
				goodneutron =  true;
				nleadindex = 0;
			}
			//If nMult > 1: Take nHit and check if good event and give back leading hit index and boolean
			if (nMult > 1) {
				//pass Nhit array, multiplicity and reference to leadindex which will be modified by function
				goodneutron = goodNeutronEvent(nHit, nMult, nleadindex, MC_DATA_OPT);
			}

			// Fill tree based on d(e,e'n)X for data
			if( (nMult == 1 || (nMult > 1 && goodneutron) )&& MC_DATA_OPT == 1 ){
				outTree->Fill();
			} // else fill tree on d(e,e')nX for MC
			else if( MC_DATA_OPT == 0 ||  MC_DATA_OPT == 2 ){
				outTree->Fill();
			}



		} // end loop over events
	}// end loop over files

	outFile->cd();
	outTree->Write();
	outFile->Close();

	return 0;
}
