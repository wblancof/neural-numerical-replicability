//============================================================================
// Name        : SpikeTrain.cpp
// Created on  : May, 2016
// Author      : 
// Description :
//   Simple module to manage an Spike train. It also safe/write to a file 
//   (.txt and .mat) the spike times series
// Used for    :  HH_Blanco2016_doubleP.cpp
//   network of 100 HH (reduced) cells with heterogeneous input current (Ii)
//   all to all coupling with synaptic depression
//   From Tabak, Mascagni, Bertram. J Neurophysiol, 103:2208-2221, 2010.
//
//   New version:
//   - some % are inhibitory neurons
//
//   From Blanco, Tabak, Bertram. J Frontiers in Comp. Neuroscience, 2017.
//============================================================================

#include "SpikeTrain_allP.h"

#include <iostream>
#include <fstream>      // std::ofstream
#include <vector>

using namespace std;

SpikeTrain::SpikeTrain(const unsigned int n) {
    // TODO Auto-generated constructor stub
    if (n<=0) {
        cerr << "SpikeTrain <constructor> parameter wrong!! Value should be integer > 0" << endl;
        exit(-1);
    }

    this->nNeurons = n;
    // To set values
    for(unsigned int i = 0; i < n; i++)
    {
        vector<double> temp; // create an array, don't work directly on buff yet.
        mSpikeTrain.push_back(temp); // Store the array in the buffer
    }
}

SpikeTrain::~SpikeTrain() {
    // TODO Auto-generated destructor stub

    // Releasing of spike times
    for (unsigned n = 0; n < mSpikeTrain.size(); ++n) {
        mSpikeTrain[n].clear();
    }
    // Releasing the neurons
    mSpikeTrain.clear();

}

void SpikeTrain::addSpikeTimeToNeuron(const unsigned int n, const double t) {

    if (n>=this->mSpikeTrain.size()) {
        cerr << "SpikeTrain <addSpikeTimeToNeuron> Neuron index out of range!!\n No spike added" << endl;
    }

    if (mSpikeTrain[n].size() == 0) { // The first time
        mSpikeTrain[n].push_back(t);
        return;
    }

    double lastTime = mSpikeTrain[n][mSpikeTrain[n].size()-1];
    if (lastTime < t) {
        mSpikeTrain[n].push_back(t);
    } else
        cerr << "SpikeTrain <addSpikeTimeToNeuron> Last spike time > current spike time!!\n No spike time added" << endl;
}

string SpikeTrain::toString(const unsigned int n, bool labels = false) const {

    if (n>=this->mSpikeTrain.size()) {
        cerr << "SpikeTrain <toString> Neuron index out of range!!\n" << endl;
        return "";
    }

    string strout ("");

    if (labels)
        strout = "Neuron(" + to_string(n) + "): ";

    vector<double> vST = mSpikeTrain[n];

    // List of spike times
    for (vector<double>::iterator st = vST.begin(); st != vST.end(); ++st) {
        strout = strout + " " + to_string(*st);
    }

    return strout;
}

string SpikeTrain::toString( bool labels) const { // be careful with big data

    string strout ("\n");
    if (labels)
        strout = strout + "Spike Train matrix:" + '\n' + "=====================" + '\n';

    //Loop through the neurons (rows)
    for(unsigned int neuronI = 0; neuronI < mSpikeTrain.size(); ++neuronI) {
        strout = strout + toString(neuronI, labels) + '\n';
    }

    return strout;
}

void SpikeTrain::printToTXTFile( string const strFileName) {


    ofstream myfile(strFileName);

    string strout ("\n");

    cout << "Writing in file: "<< strFileName << endl;

    if (myfile.is_open())
    {
        if (this->mSpikeTrain.size() > 0) {
            // I can call toString( bool labels = false) but since the matrix could be big
            for(unsigned int neuronI = 0; neuronI < mSpikeTrain.size(); ++neuronI) {
                    myfile << toString(neuronI, false);
            }
            myfile.close();
        }
    } else cerr << "Unable to open file: "<< strFileName << endl;

}

void SpikeTrain::printToMatlabFile (string const strFileName, int n_p) {

    ofstream myfile(strFileName);

    cout << "Writing in file: "<< strFileName << endl;

    myfile.precision(n_p);                              // Adjust precision
    myfile.setf( std::ios::fixed, std::ios::floatfield );
    
    if (myfile.is_open())
    {
        if (this->mSpikeTrain.size() > 0) {
            // I can call toString( bool labels = false) but since the matrix could be big
            for(unsigned int neuronI = 0; neuronI < mSpikeTrain.size(); ++neuronI) {
                myfile << "spikeTimes{"<< to_string(neuronI+1)<<  "} = [" << toString(neuronI, false)<<"];"<<'\n';
            }
            myfile.close();
        }
    } else cerr << "Unable to open file: "<< strFileName << endl;
}


void SpikeTrain::printToConsole() {

    cout << this->toString(true);
}
