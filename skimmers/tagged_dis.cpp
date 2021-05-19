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

using namespace std;


int main(int argc, char** argv) {
	// check number of arguments
	if( argc < 6 ){
		cerr << "Incorrect number of arugments. Instead use:\n\t./code [outputFile] [MC/DATA] [load shift] [inputFile] [gen rootFile]\n\n";
		cerr << "\t\t[outputFile] = ____.root\n";
		cerr << "\t\t[<MC,DATA> = <0, 1> \n";
		cerr << "\t\t[<load shifts N,Y> = <0, 1> \n";
		cerr << "\t\t[inputFile] = ____.hipo \n";
		cerr << "\t\t[gen rootFile] = band_tagged_____.root \n";
		return -1;
	}

	int MC_DATA_OPT = atoi(argv[2]);
	int loadshifts_opt = atoi(argv[3]);

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
	double rad_weight		= 0;
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
	outTree->Branch("rad_weight"	,&rad_weight		);
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

	shiftsReader shifts;
	double * FADC_INITBAR;
	double * TDC_INITBAR;
	if( loadshifts_opt ){
		// Load bar shifts
		shifts.LoadInitBarFadc	("../include/FADC_pass1v0_initbar.txt");
		FADC_INITBAR = (double*) shifts.getInitBarFadc();
		shifts.LoadInitBar	("../include/TDC_pass1v0_initbar.txt");
		TDC_INITBAR = (double*) shifts.getInitBar();
		// Load run-by-run shifts
		// 	for 10.2 these are not needed
		//shifts.LoadInitRunFadc("../include/FADC_pass1v0_initrun.txt");
		//FADC_INITRUN = (double*) shifts.getInitRunFadc();
	}
	// Effective velocity for re-doing x- calculation
	double * FADC_EFFVEL_S6200;
	double *  TDC_EFFVEL_S6200;
	double * FADC_EFFVEL_S6291;
	double *  TDC_EFFVEL_S6291;
	double *  FADC_LROFF_S6200;
	double *   TDC_LROFF_S6200;
	double *  FADC_LROFF_S6291;
	double *   TDC_LROFF_S6291;
	shifts.LoadEffVel	("../include/EffVelocities_S6200.txt",	"../include/EffVelocities_S6291.txt");
	shifts.LoadLrOff	("../include/LrOffsets_S6200.txt",	"../include/LrOffsets_S6291.txt");
	FADC_EFFVEL_S6200	= (double*) shifts.getFadcEffVel(6200);
	TDC_EFFVEL_S6200	= (double*)  shifts.getTdcEffVel(6200);
	FADC_EFFVEL_S6291	= (double*) shifts.getFadcEffVel(6291);
	TDC_EFFVEL_S6291	= (double*)  shifts.getTdcEffVel(6291);

	FADC_LROFF_S6200	= (double*) shifts.getFadcLrOff(6200);
	TDC_LROFF_S6200		= (double*)  shifts.getTdcLrOff(6200);
	FADC_LROFF_S6291	= (double*) shifts.getFadcLrOff(6291);
	TDC_LROFF_S6291		= (double*)  shifts.getTdcLrOff(6291);

	//Maps for geometry positions
	std::map<int,double> bar_pos_x;
	std::map<int,double> bar_pos_y;
	std::map<int,double> bar_pos_z;
	//Load geometry position of bars
	getBANDBarGeometry("../include/band-bar-geometry.txt", bar_pos_x, bar_pos_y,bar_pos_z);
	//Maps for energy deposition 
	std::map<int,double> bar_edep;
	//Load edep calibration of bars if not MC
	if( MC_DATA_OPT == 1){ //Data
		getBANDEdepCalibration("../include/band-bar-edep.txt", bar_edep);
	}
	else if( MC_DATA_OPT == 0){ //MC
		getBANDEdepCalibration("../include/band-bar-edep-mc.txt", bar_edep);
	}
	else {
		cout << "No BAND Edep file is loaded " << endl;
	}


	TFile * mc_gen_file = NULL;
	TTree * mc_gen_tree = NULL;
	double mc_gen_weight = -1;
	double mc_gen_pe[3];
	double mc_gen_pn[3];
	// Load input file
	if( MC_DATA_OPT == 0){
		int runNum = 11;
		Runno = runNum;
		// Load root file to get the radiative weight
		mc_gen_file = new TFile(argv[5]);
		mc_gen_tree = (TTree*) mc_gen_file->Get("T");
		mc_gen_tree->SetBranchAddress("rad",&mc_gen_weight);
		mc_gen_tree->SetBranchAddress("pe",mc_gen_pe);
		mc_gen_tree->SetBranchAddress("pn",mc_gen_pn);
	}
	else if( MC_DATA_OPT == 1){
		int runNum = getRunNumber(argv[4]);
		Runno = runNum;
		auto cnd = connection.GetCondition(runNum, "beam_energy");
		Ebeam = cnd->ToDouble() / 1000.; // [GeV]
		current = connection.GetCondition( runNum, "beam_current") ->ToDouble(); // [nA]
	}
	else{
		exit(-1);
	}

	// Setup hipo reading for this file
	TString inputFile = argv[4];
	hipo::reader reader;
	reader.open(inputFile);
	hipo::dictionary  factory;
	hipo::schema	  schema;
	reader.readDictionary(factory);
	BEvent		event_info		(factory.getSchema("REC::Event"		));
	BBand		band_hits		(factory.getSchema("BAND::hits"		));
	hipo::bank	scaler			(factory.getSchema("RUN::scaler"	));
	hipo::bank  run_config (factory.getSchema("RUN::config"));
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
	while(reader.next()==true){
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
		rad_weight = -1;
		genpart mcPart[maxGens];
		mcParts->Clear();


		// Count events
		if(event_counter%10000==0) cout << "event: " << event_counter << endl;
		//if( event_counter > 100000 ) break;
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

		//Get Event number from RUN::config
		eventnumber = run_config.getInt( 1 , 0 );

		// Get integrated charge, livetime and start-time from REC::Event
		if( event_info.getRows() == 0 ) continue;
		getEventInfo( event_info, gated_charge, livetime, starttime );

		// For simulated events, get the weight for the event
		if( MC_DATA_OPT == 0){
			getMcInfo( mc_particle , mc_event_info , mcPart , starttime, weight, Ebeam , genMult );
			mc_gen_weight = -1;
			mc_gen_pe[0]=0; mc_gen_pe[1]=0; mc_gen_pe[2]=0;
			mc_gen_pn[0]=0; mc_gen_pn[1]=0; mc_gen_pn[2]=0;
			mc_gen_tree->GetEntry(eventnumber-2);
			rad_weight = mc_gen_weight;

			double check_pe[3] = {mc_particle.getFloat(1,0),mc_particle.getFloat(2,0),mc_particle.getFloat(3,0)};
			double check_pn[3] = {mc_particle.getFloat(1,1),mc_particle.getFloat(2,1),mc_particle.getFloat(3,1)};
			if( fabs( check_pe[0]-mc_gen_pe[0] ) > 0.001 ||
			    fabs( check_pe[1]-mc_gen_pe[1] ) > 0.001 ||
			    fabs( check_pe[2]-mc_gen_pe[2] ) > 0.001 ||
			    fabs( check_pn[0]-mc_gen_pn[0] ) > 0.001 ||
			    fabs( check_pn[1]-mc_gen_pn[1] ) > 0.001 ||
			    fabs( check_pn[2]-mc_gen_pn[2] ) > 0.001 ){
				cerr << "ISSUE WITH MATCHING\n";
				exit(-1);
			}
		}


		// Grab the neutron information:
		// 											do the hotfix for x-position
		if( MC_DATA_OPT == 0 ){
			getNeutronInfo( band_hits, band_rawhits, band_adc, band_tdc, nMult, nHit , starttime , Runno, bar_pos_x, bar_pos_y, bar_pos_z, bar_edep);
		}
		else{
			getNeutronInfo( band_hits, band_rawhits, band_adc, band_tdc, nMult, nHit , starttime , Runno, bar_pos_x, bar_pos_y, bar_pos_z, bar_edep,
					1, 	FADC_LROFF_S6200,	TDC_LROFF_S6200,
						FADC_LROFF_S6291,	TDC_LROFF_S6291,
						FADC_EFFVEL_S6200,	TDC_EFFVEL_S6200,
						FADC_EFFVEL_S6291,	TDC_EFFVEL_S6291	);
		}
		if( loadshifts_opt ){
			for( int n = 0 ; n < nMult ; n++ ){
				nHit[n].setTofFadc(	nHit[n].getTofFadc() 	- FADC_INITBAR[(int)nHit[n].getBarID()] );
				nHit[n].setTof(		nHit[n].getTof() 	- TDC_INITBAR[(int)nHit[n].getBarID()]  );
			}
		}



		// Grab the electron information:
		getElectronInfo( particles , calorimeter , scintillator , DC_Track, DC_Traj, eHit , starttime , Runno , Ebeam );


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
		else if( MC_DATA_OPT == 0 ){
			outTree->Fill();
		}



	} // end loop over events

	outFile->cd();
	outTree->Write();
	outFile->Close();

	return 0;
}
