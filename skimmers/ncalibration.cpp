#include <iostream>

#include "TFile.h"
#include "TTree.h"
#include "TVector3.h"
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

using namespace std;


int main(int argc, char** argv) {
	// check number of arguments
	if( argc < 5 ){
		cerr << "Incorrect number of arugments. Instead use:\n\t./code [outputFile] [MC/DATA] [inputFile] \n\n";
		cerr << "\t\t[outputFile] = ____.root\n";
		cerr << "\t\t[<MC,DATA> = <0, 1> \n";
		cerr << "\t\t[<load shifts N,Y> = <0, 1> \n";
		cerr << "\t\t[inputFile] = ____.hipo ____.hipo ____.hipo ...\n\n";
		return -1;
	}

	int MC_DATA_OPT = atoi(argv[2]);
	int loadshifts_opt = atoi(argv[3]);

	// Create output tree
	TFile * outFile = new TFile(argv[1],"RECREATE");
	TTree * outTree = new TTree("calib","BAND Neutrons and CLAS Electrons");
	//	Event info:
	int Runno		= 0;
	double Ebeam		= 0;
	double gated_charge	= 0;
	double livetime		= 0;
	double starttime	= 0;
	double current		= 0;
	int eventnumber = 0;
	bool goodneutron 	= false;
	int nleadindex 		= -1;
	// 	Neutron info:
	int nMult		= 0;
	TClonesArray * nHits 	= new TClonesArray("bandhit");
	TClonesArray &saveHit 	= *nHits;
	//	Electron info:
	clashit eHit;
	// 	Event branches:
	outTree->Branch("Runno"		,&Runno			);
	outTree->Branch("Ebeam"		,&Ebeam			);
	outTree->Branch("gated_charge"	,&gated_charge		);
	outTree->Branch("livetime"	,&livetime		);
	outTree->Branch("starttime"	,&starttime		);
	outTree->Branch("current"	,&current		);
	outTree->Branch("eventnumber",&eventnumber);
	//	Neutron branches:
	outTree->Branch("nMult"		,&nMult			);
	outTree->Branch("nHits"		,&nHits			);
	//Branches to store if good Neutron event and leadindex
	outTree->Branch("goodneutron"		,&goodneutron	);
	outTree->Branch("nleadindex"		,&nleadindex			);
	//	Electron branches:
	outTree->Branch("eHit"		,&eHit			);

	// Connect to the RCDB
	rcdb::Connection connection("mysql://rcdb@clasdb.jlab.org/rcdb");

	//Load Bar shifts
	shiftsReader shifts;
	double * FADC_BARSHIFTS;
	double * TDC_BARSHIFTS;

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

	// Load the electron PID class:
	e_pid ePID;
	// Load the DC fiducial class for electrons;
	DCFiducial DCfid_electrons;

	// Load input file
	for( int i = 4 ; i < argc ; i++ ){
		if( MC_DATA_OPT == 0){
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


		// Loop over all events in file
		int event_counter = 0;
		gated_charge 	= 0;
		livetime	= 0;
		int run_number_from_run_config = 0;
		double torussetting = 0;
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
			// Electron
			eHit.Clear();


			// Count events
			if(event_counter%10000==0) cout << "event: " << event_counter << endl;
			//if( event_counter > 1000000 ) break;
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


			//Get Event number and run number from RUN::config
			run_number_from_run_config = run_config.getInt( 0 , 0 );
			eventnumber = run_config.getInt( 1 , 0 );
			if (run_number_from_run_config != Runno && event_counter < 100) {
					cout << "Run number from RUN::config and file name not the same!! File name is " << Runno << " and RUN::config is " << run_number_from_run_config << endl;
			}

			if( loadshifts_opt && event_counter == 1 && MC_DATA_OPT !=0){
				//Load of shifts depending on run number
				if (Runno >= 11286 && Runno < 11304)	{ //LER runs
					shifts.LoadInitBarFadc("../include/LER_FADC_shifts.txt");
					FADC_BARSHIFTS = (double*) shifts.getInitBarFadc();
					shifts.LoadInitBar("../include/LER_TDC_shifts.txt");
					TDC_BARSHIFTS = (double*) shifts.getInitBar();
				}
				else if (Runno > 6100 && Runno < 6800) { //Spring 19 data
					shifts.LoadInitBarFadc	("../include/FADC_pass1v0_initbar.txt");
					FADC_BARSHIFTS = (double*) shifts.getInitBarFadc();
					shifts.LoadInitBar	("../include/TDC_pass1v0_initbar.txt");
					TDC_BARSHIFTS = (double*) shifts.getInitBar();
				}
				else {
					cout << "No bar by bar offsets loaded " << endl;
					cout << "Check shift option when starting program. Exit " << endl;
					exit(-1);
				}
			}

			//from first event get RUN::config torus Setting
			// inbending = negative torussetting, outbending = torusseting
			torussetting = run_config.getFloat( 7 , 0 );

			// Get integrated charge, livetime and start-time from REC::Event
			if( event_info.getRows() == 0 ) continue;
			getEventInfo( event_info, gated_charge, livetime, starttime );

			// Skim the event so we only have a single electron and NO other particles:
			int nElectrons = 0;
			for( int part = 0 ; part < particles.getRows() ; part++ ){
				int PID 	= particles.getPid(part);
				int charge 	= particles.getCharge(part);
				if( PID != 11 ) 					continue;
				if( charge !=-1 ) 					continue;
				TVector3	momentum = particles.getV3P(part);
				TVector3	vertex	 = particles.getV3v(part);
				TVector3 	beamVec(0,0,Ebeam);
				TVector3	qVec; qVec = beamVec - momentum;
				double EoP=	calorimeter.getTotE(part) /  momentum.Mag();
				double Epcal=	calorimeter.getPcalE(part);
				if( EoP < 0.17 || EoP > 0.3 	) 			continue;
				if( Epcal < 0.07		) 			continue;
				if( vertex.Z() < -8 || vertex.Z() > 3 )			continue;
				if( momentum.Mag() < 3 || momentum.Mag() > Ebeam)	continue;
				double lV=	calorimeter.getLV(part);
				double lW=	calorimeter.getLW(part);
				if( lV < 15 || lW < 15		) 			continue;
				double Omega	=Ebeam - sqrt( pow(momentum.Mag(),2) + mE*mE )	;
				double Q2	=	qVec.Mag()*qVec.Mag() - pow(Omega,2)	;
				double W2	=	mP*mP - Q2 + 2.*Omega*mP	;
				if( Q2 < 2 || Q2 > 10			) 		continue;
				if( W2 < 2*2				) 		continue;

				nElectrons++;
			}
			if( nElectrons != 1 ) 	continue;
			//if( nOthers != 0 )	continue;


			// Grab the electron information:
			getElectronInfo( particles , calorimeter , scintillator , DC_Track, DC_Traj, eHit , starttime , Runno , Ebeam );
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
			if( loadshifts_opt && MC_DATA_OPT !=0){
					for( int n = 0 ; n < nMult ; n++ ){
						nHit[n].setTofFadc(	nHit[n].getTofFadc() 	- FADC_BARSHIFTS[(int)nHit[n].getBarID()] );
						nHit[n].setTof(		nHit[n].getTof() 	- TDC_BARSHIFTS[(int)nHit[n].getBarID()]  );
					}
			}

			// Store the neutrons in TClonesArray
			for( int n = 0 ; n < nMult ; n++ ){
				new(saveHit[n]) bandhit;
				saveHit[n] = &nHit[n];
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
	}// end loop over files

	outFile->cd();
	outTree->Write();
	outFile->Close();

	return 0;
}
