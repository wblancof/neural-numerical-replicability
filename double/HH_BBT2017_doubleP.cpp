//============================================================================
// Name        : HH_Blanco2016_doubleP.cpp
// Created on  : May, 2016
// Author      : 
// Description :
//   Original version:
//   network of 100 HH (reduced) cells with heterogeneous input current (Ii)
//   all to all coupling with synaptic depression
//   From Tabak, Mascagni, Bertram. J Neurophysiol, 103:2208-2221, 2010.
//
//   New version:
//   - some % are inhibitory neurons
//
//   From Blanco, Tabak, Bertram. J Frontiers in Comp. Neuroscience, 2017.
//============================================================================

#include <iostream>     // std::cout
#include <fstream>      // std::ofstream
#include <string>     	// std::string, std::to_string
#include <cmath>        // pow

#include <boost/array.hpp>
#include <boost/numeric/odeint.hpp>

#include "SpikeTrain_doubleP.h"
#include "iappDist_doubleP.h"

using namespace std;
using namespace boost::numeric::odeint;

// Simulation constants
const unsigned int nNeurons = 100;
const double dt = 0.01;							// Time step: ms
const double maxTimeSimulation = 8000;  		// Maximum simulation time: ms
const bool SAVE_SIMULATION = true;
const int n_precision = 14;

// Simulation parameters
double pExcNeurons = 1; 										// Percentage of excitatory neurons
unsigned int nExcNeurons = (unsigned int) nNeurons*pExcNeurons;	// Amount of excitatory neurons
unsigned int nInhNeurons = nNeurons-nExcNeurons;				// Amount of inhibitory neurons
unsigned int maxNumBurst = 200;									// Amount of burst to be reached to stop the simulation

// Parameters needed to detect episodes
const double thA = 0.1730;										// thA = 0.25*(maxAt-minAt); based on Patrick paper. Previous calculated in Matlab
const double thDA = 0.1490;
bool activePhase;
unsigned int burstCount;

// Parameters needed to detect spikes
bool depolarization[nNeurons];

// Still are not used
const double timeTotrash = 1000; 				// Time discarded (transient behavior) from the beginning of the simulation,
const double timeForParameters = 20000; 		// Time to calculate parameters as

// Cellular parameters
// p i0=-10. deli=15
// p vna=115  vk=-12  vl=10.6  gnabar=36  gkbar=12  gl=0.1
// p h0=0.8
double i0=-10.0, deli=15,
		vna=115, vl=10.6,  gnabar=36,  gkbar=12,  gl=0.1,
		h0=0.8;

// p vexc=70
// p vinh = -12
double vExc = 70, vsyn = 70,
		vInh = -12, vk = -12;

// Synaptic parameters
// p taus=10 tauf=1
// p gsyn=3.6 vsyn=70
// p alphad=0.0015 betad=0.12
// p Vthresh=40 kv=1
const double taus=10, tauf=1,
		gsyn=3.6/nNeurons,
		alphad=0.0015, betad=0.12,
		Vthresh=40, kv=1;

//# Create a table of applied currents Ii
// Table iapp % 100 0 99 i0+t*deli/99
vector<double> iapp(nNeurons);	 				// for Excitatory neurons
double iappj;

//# Synaptic drive from excitatory cells
double atotExc, atotExcj;
//# Synaptic drive from inhibitory cells
double atotInh, atotInhj;

//# v[i] membrane potential of cell i
//# n[i] activation of K+ conductance for cell i
//# a[i] synaptic drive from cell i
//# s[i] synaptic recovery for terminals from cell i
typedef boost::array<double, 4> NeuronState_v_n_a_s;
typedef boost::array<double, 4> myArrayDouble4;

// Vector of neurons -> The Network
vector<NeuronState_v_n_a_s> network(nNeurons);

// SpikeTrain Class to manage/store/save the spike times for each neuron
SpikeTrain *mSpikeTrain = new SpikeTrain(nNeurons);

// Auxiliary variables for output
vector<NeuronState_v_n_a_s> ave; 			// Average of V N A and S
vector<myArrayDouble4> networkOutputs; 		// Average Ainh and Aexc, episodes time,

//==========
// Functions
//==========

// Rate constants and steady state activation function for Na+ and K+ currents
// am(v)=.1*(25-v)/(exp(.1*(25-v))-1)
inline double am (const double v) {
	return .1*(25-v)/(exp(.1*(25-v))-1);
}

// bm(v)=4.0*exp(-v/18)
inline double bm (const double v) {
	return 4.0*exp(-v/18);
}

// minf(v)=am(v)/(am(v)+bm(v))
inline double minf (const double v) {
	return am(v)/(am(v)+bm(v));
}

// bn(v)=.125*exp(-v/80)
inline double bn (const double v) {
	return .125*exp(-v/80);
}

// an(v)=.01*(10-v)/(exp(.1*(10-v))-1.0)
inline double an (const double v) {
	return .01*(10-v)/(exp(.1*(10-v))-1.0);
}

// Synaptic activation
//fsyn(V) = 1./(1+exp((Vthresh-V)/kv))
inline double fsyn (const double v) {
	//return 1.0/(1+exp((Vthresh-v)/kv));
	return 1.0/(1+exp(Vthresh-v)); // if kv = 1;
}

// System ODEs per Neuron
void HH_NeuronModel( const NeuronState_v_n_a_s &x , NeuronState_v_n_a_s &dxdt , double t )
{
	double vj = x[0];
	double nj = x[1];
	double aj = x[2];
	double sj = x[3];

	// Differential equations (XPP original version)
	// v[i] membrane potential of cell i
	// v[0..99]'= -gl*(v[j]-vl)
	//				-gnabar*minf(v[j])^3*(h0-n[j])*(v[j]-vna)
	//        		-gkbar*n[j]^4*(v[j]-vk)
	// 				-gsyn*(atot-a[j]*s[j]/100)*(v[j]-vsyn)
	//				+iapp([j])
	dxdt[0] = -gl*(vj-vl)
					-gnabar*pow(minf(vj),3)*(h0-nj)*(vj-vna)
					-gkbar*pow(nj,4)*(vj-vk)
					-gsyn*atotExcj*(vj-vExc)
					-gsyn*atotInhj*(vj-vInh)
					+iappj;

	// n[i] Activation of K+ conductance for cell i
	// n[0..99]'= an(v[j])-(an(v[j])+bn(v[j]))*n[j]
	dxdt[1] = an(vj)-(an(vj)+bn(vj))*nj;

	// a[i] Synaptic drive from cell i
	// a[0..99]'= fsyn(v[j])*(1-a[j])/tauf - a[j]/taus
	dxdt[2] = fsyn(vj)*(1-aj)/tauf - aj/taus;

	// s[i] Synaptic recovery for terminals from cell i
	// s[0..99]'=alphad*(1-s[j])-betad*fsyn(v[j])*s[j]
	dxdt[3] = alphad*(1-sj)-betad*fsyn(vj)*sj;
}

void initEachNeuron( void) {
	// Initial conditions
	// init v[0..99]=0 n[j]=0 a[j]=0.01 s[j]=.25
	for (unsigned int n = 0; n < nNeurons; n++) {
		network[n] = {0.0, 0.0, 0.01, 0.25};	// order: v, n, a, s
		depolarization[n]=false;					// no spike found
	}
}

// Function to write a .txt file with the output parameters of the network state
int writeToFile (string const fileNameStr, vector<NeuronState_v_n_a_s> &v){

	ofstream myfile(fileNameStr);
	cout << "Writing in file: "<< fileNameStr << endl;

	myfile.precision(n_precision);								// Adjust precision
	myfile.setf( std::ios::fixed, std::ios::floatfield );
	if (myfile.is_open()) {
		if (v.size() > 0) {
			for (unsigned int i=0; i<v.size();i++) {
				myfile << v[i][0] << '\t' << v[i][1] << '\t' << v[i][2] << '\t' << v[i][3] << '\n';
			}
			myfile.close();
		}
	} else cout << "Unable to open file: "<< fileNameStr << endl;

	return 0;
}

void showParameters( ){
	cout << "gcc version: "<< __GNUC__<<"."<<__GNUC_MINOR__<<"."<<__GNUC_PATCHLEVEL__<<endl;
    cout << "Sizes (bytes):\n"
			<<"float = "<< sizeof(float)
			<<", double = "<< sizeof(double)
			<<", double = "<< sizeof(double) << "\n\n";
		
	cout << "Running with the parameters:\n"
			<< "Total Neurons = "<< to_string(nNeurons) << "\n"
			<< "Exc Neurons = "<< to_string(nExcNeurons) << "\n"
			<< "Inh Neurons = "<< to_string(nInhNeurons) << "\n"
			<< "maxTimeSimulation = "<< to_string(maxTimeSimulation/1000) << " s\n"
			<< "nBurst = "<< to_string(maxNumBurst) << "\n"
			<< "dt = "<< to_string(dt) << "\n"
			<< "vInh = "<< to_string(vInh) << "\n"
			<< "Float Precision = "<< to_string(n_precision) << "\n"
			<< "SAVE_SIMULATION = "<< to_string(SAVE_SIMULATION) << "\n"
			<< endl;
}


void showUsage(string name)
{
	cerr << "Usage: " << name << " <option(s)> SOURCES"
			<< "Options:\n"
			<< "\t-h,--help\t\tShow this help message\n"
			<< "\t-vInh\t<Reversal Potential value, double [-12 .. 70]>\n"
			<< "\t-nBurst\t<How many burst, integer > 0>\n"
			<< "\t-pExcN\t<Persentage of excitatory neurons, double ]0..1]>\n"
			<< endl;
}

int parseParameters(int argc, char* argv[]) {

	if (argc < 2) {
		showUsage(argv[0]);
		return -1;
	}

	for (int i = 1; i < argc; i+=2) {
		if (string(argv[i]) == "-vInh") {
			if (i + 1 < argc) { // Make sure we aren't at the end of argv!
				vInh = strtod(argv[i + 1], NULL); // [70 ..-12]
				if (vInh<-12.0 || vInh>70.0) {
					std::cerr << "-vInh option requires double argument [-12..70]." << std::endl;
					return -1;
				}
			} else { // Uh-oh, there was no argument to the destination option.
				std::cerr << "-vInh option requires one argument." << std::endl;
				return -1;
			}
		}
		if (string(argv[i]) == "-nBurst") {
			if (i + 1 < argc) { // Make sure we aren't at the end of argv!
				maxNumBurst = strtod(argv[i + 1],NULL); // Increment 'i' so we don't get the argument as the next argv[i].
			} else { // Uh-oh, there was no argument to the destination option.
				std::cerr << "-nBurst option requires one argument." << std::endl;
				return -1;
			}
		}
		if (string(argv[i]) == "-pExcN") {
			if (i + 1 < argc) { // Make sure we aren't at the end of argv!
				pExcNeurons = strtod(argv[i + 1],NULL); // Increment 'i' so we don't get the argument as the next argv[i].
				if (pExcNeurons<=0 || pExcNeurons>1) {
					std::cerr << "-pExcN option requires double argument ]0..1]." << std::endl;
					return -1;
				}
			} else { // Uh-oh, there was no argument to the destination option.
				std::cerr << "-pExc option requires double argument ]0..1]." << std::endl;
				return -1;
			}
		}
	}

	return 1;
}


int main(int argc, char* argv[]) {

	unsigned int n;
	double t;
	double actTotalExc, actTotalInh, v_1, s_t;
	double sTotalExc, sTotalInh;

	NeuronState_v_n_a_s sN_1, sN, dxdtinout;
	vector<myArrayDouble4> sv1, sv2; 				// values of S for the key neurons
	myArrayDouble4 sA, s1, s2;

	vector<NeuronState_v_n_a_s> outputAct;
	runge_kutta4< NeuronState_v_n_a_s > stepper;		// Solver/stepper from ODEint library

	if (parseParameters(argc, argv)<0)
		exit(0);

	// initialize random seed:
	srand (time(NULL));
	clock_t te;

	// Initial conditions applied current for every neuron
	// The same distribution for all simulations
	// =========================================
	initRandIappValues(iapp);
	
	
	nExcNeurons = (int) nNeurons*pExcNeurons;		// amount of excitatory neurons
	nInhNeurons = nNeurons-nExcNeurons;				// amount of inhibitory neurons

	// Simulation
	// ==================================
	cout << "Simulation ...!!!" << endl;
	showParameters();

	//# Initial conditions for every neuron
	// ===================================
	initEachNeuron();
	activePhase = false;
	t=0.0; burstCount = 0;

	te = clock();									// To get the simulation time
	cout.precision(4);								// Adjust precision to cout
	std::cout.setf( std::ios::fixed, std:: ios::floatfield );
	while (burstCount<maxNumBurst and t<=maxTimeSimulation) {

		// Total synaptic drive exc/inh for all cells
		atotExc = atotInh = 0.0;

		// Synaptic drive from excitatory cells
		// atotexc=sum(0,100)of(shift(s0,i')*shift(a0,i'))/100
		// remembering the order: v, n, a, s -> 0, 1, 2, 3
		for ( n = 0; n < nExcNeurons; n++) {
			atotExc += network[n][3]*network[n][2];
		}
		// Synaptic drive from inhibitory cells
		for ( n = nExcNeurons; n < nNeurons; n++) {
			atotInh += network[n][3]*network[n][2];
		}

		// Saving the previous state of the network to detecting episodes
		sN_1[0] = sN_1[1] = sN_1[2] = sN_1[3] = 0.0;
		for (n = 0; n < nNeurons; n++) {
			sN_1[0] +=network[n][0];
			sN_1[1] +=network[n][1];
			sN_1[2] +=network[n][2];
			sN_1[3] +=network[n][3];
		}
		sN_1[0] = sN_1[0]/nNeurons; sN_1[1] = sN_1[1]/nNeurons; sN_1[2] = sN_1[2]/nNeurons; sN_1[3] = sN_1[3]/nNeurons;

		// Initialize the Network state
		sN[0] = sN[1] = sN[2] = sN[3] = 0.0;
		// For each neuron
		for ( n = 0; n < nNeurons; n++) {
			// Synaptic drive exc/inh
			atotExcj = atotExc; atotInhj = atotInh;
			// Taking out the contribution of the own cell i
			iappj = iapp[n]; 								// Get the original distribution [-10 .. 5] iapp dist
			if (n<nExcNeurons) { //excitatory
				atotExcj -= network[n][3]*network[n][2];
			} else { // inhibitory
				atotInhj -= network[n][3]*network[n][2]; 	// remove its own synapse
			}

			v_1 = network[n][0];							// Save previous voltage, used to detect spikes
			// Integration, solving the ODE
			stepper.do_step( HH_NeuronModel,
						network[n],
						t,
						dt);

			// Detecting spikes
			// Detecting Depolarization
			if (~depolarization[n] & 				// It is not already on Depolarization period
					network[n][0]>=Vthresh &		// Voltage >= Vth
					(network[n][0]-v_1)/dt>0) {     // Positive slope
				depolarization[n] = true;
				s_t = t;							// Saving time value
			}
			// Detecting Repolarization
			if ( depolarization[n] &				// It was on Depolarization period
					network[n][0]<=Vthresh &		// Voltage <= Vth
					(network[n][0]-v_1)/dt<0) {		// Negative slope
				depolarization[n] = false;
				mSpikeTrain->addSpikeTimeToNeuron(n,t);
			}

			// Accumulating/Sum for each neuron state
			sN[0] +=network[n][0];
			sN[1] +=network[n][1];
			sN[2] +=network[n][2];
			sN[3] +=network[n][3];
		}

		// Normalize the current state of the network
		sN[0] = sN[0]/nNeurons; sN[1] = sN[1]/nNeurons; sN[2] = sN[2]/nNeurons; sN[3] = sN[3]/nNeurons;
		
		// Population activity and synaptic recovery for excitatory neurons
		actTotalExc=sTotalExc= 0;
		for ( n = 0; n < nExcNeurons; n++) {
			actTotalExc += network[n][2];
			sTotalExc += network[n][3];
		}

		// Population activity and synaptic recovery for inhibitory neurons
		actTotalInh=sTotalInh= 0;
		for ( n = nExcNeurons; n < nNeurons; n++) {
			actTotalInh += network[n][2];
			sTotalInh += network[n][3];
		}

		// Detecting episode
		sA[2] = 0.0;
		if (~activePhase & 							// It is not already on active phase
				sN[2]>=thA &						// Activity > thA
				(sN[2]-sN_1[2])/dt>thDA) {			// Activity derivative > thDA
			activePhase = true;

			// Save values of V A N and S
			sA[0] = actTotalExc/nExcNeurons;
			sA[1] = actTotalInh/nInhNeurons;
			sA[2] = t;
			sA[3] = 1;
			networkOutputs.push_back(sA); 			// Aexc, Ainh, time episode, 1
		} else if (activePhase & 					// It was on active phase
				sN[2]<thA) {						// Activity < thA
			activePhase = false;
			++burstCount;							// Counting the episode
			cout << "<" << nExcNeurons << "," << vInh << ">" << "- burst:" << burstCount << ", time: "<< t << endl;

			// Save values of V A N and S
			sA[0] = actTotalExc/nExcNeurons;
			sA[1] = actTotalInh/nInhNeurons;
			sA[2] = -t;
			sA[3] = 1;
			networkOutputs.push_back(sA); 			// Aexc, Ainh, -time episode, 1
		}

		// Saving the current state of the network
		ave.push_back(sN);
		
		// increment time
		t+= dt;
	}

	te = clock()-te;
	cout << "Simulation duration: " << ((float)te)/CLOCKS_PER_SEC <<" seconds"<< endl;
	cout << "=========================================" << endl;

	if (SAVE_SIMULATION) {
		string solver = "rk4_";
		string sdt = "dt0" + to_string((int)(dt*10000)) + "_";
		string fileNameStr = "./results/HH_BBT_" +
				solver + sdt +
				to_string(nNeurons) + "," +
				to_string(nInhNeurons) + ",vI" +
				to_string((int)vInh) + ",t=" +
				to_string((int)(maxTimeSimulation/1000)) +"s_double_IappDES.txt"; //"s_SValues72,88,86,24.txt";
		writeToFile(fileNameStr, ave);
		
		// Saving the Episodes start/end times
		fileNameStr = "./results/HH_BBT_" +
				solver + sdt +
				to_string(nNeurons) + "," +
				to_string(nInhNeurons) + ",vI" +
				to_string((int)vInh) + ",t=" +
				to_string((int)(maxTimeSimulation/1000)) +"s_double_IappDES,Epis.txt";
		writeToFile( fileNameStr, networkOutputs);

		string _underscore = "_";
		string negPosVInh;
		if (vInh < 0)
			negPosVInh = _underscore + to_string((int)abs(vInh)) + "_t";
		else
			negPosVInh = to_string((int)abs(vInh)) + "_t";

		// Saving the applied currents values
		fileNameStr = "./results/HH_BBT_" +
				solver + sdt +
				to_string(nNeurons) + "," +
				to_string(nInhNeurons) + ",vI" +
				to_string((int)vInh) + ",t=" +
				to_string((int)(maxTimeSimulation/1000)) +"s_double_IappDES,Iapp.txt";
		writeIappToFile(fileNameStr, iapp, n_precision);

		// Saving spike train in Matlab format
		fileNameStr = "./results/HH_BBT_" +
				solver + sdt +
				to_string(nNeurons) + "_" +
				to_string(nInhNeurons) + "_vI_" +
				negPosVInh +
				to_string((int)(maxTimeSimulation/1000)) + "s_double_IappDES_Spikes.m";
		mSpikeTrain->printToMatlabFile(fileNameStr, n_precision);

	}

	// Releasing the memory
	iapp.clear();
	networkOutputs.clear();
	ave.clear();
	sv1.clear();
	sv2.clear();
	delete mSpikeTrain;

	return 0;
}