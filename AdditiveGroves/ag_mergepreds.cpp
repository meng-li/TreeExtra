//Additive Groves / ag_mergepreds.cpp: main function of executable ag_mergepreds
//
//(c) Daria Sorokina

#include "LogStream.h"
#include "ErrLogStream.h"
#include "functions.h"
#include "ag_definitions.h"
#include "TrainInfo.h"
#include "ag_functions.h"
#include "INDdata.h"


#ifdef _WIN32
#include <windows.h>
#endif

#include <errno.h>
#include <sys/types.h>  
#include <sys/stat.h>   

//ag_mergepreds [-n _start_N_value_] [-a _start_alpha_value_] -d _directory1_ _directory2_ [_directory3_] 
//[_directory4_] ...
int main(int argc, char* argv[])
{	
	try{
//0. Set log file
	LogStream clog;
	LogStream::init(true);
	clog << "\n-----\nag_mergepreds ";
	for(int argNo = 1; argNo < argc; argNo++)
		clog << argv[argNo] << " ";
	clog << "\n\n";

//1. Set input parameters from command line 

	int startTiGN = 1;
	double startAlpha = 0.5;
	int firstDirNo = 0;

	stringv args(argc); 
	for(int argNo = 0; argNo < argc; argNo++)
		args[argNo] = string(argv[argNo]);

	//parse and save input parameters
	for(int argNo = 1; argNo < argc; argNo += 2)
	{
		if(!args[argNo].compare("-n"))
			startTiGN = atoiExt(argv[argNo + 1]);
		else if(!args[argNo].compare("-a"))
			startAlpha = atofExt(argv[argNo + 1]);
		else if(!args[argNo].compare("-d"))
		{
			firstDirNo = argNo + 1;
			break;
		}
		else
			throw INPUT_ERR;
	}

	//check that there are at least two directories 
	if(argc < (firstDirNo + 2))
		throw INPUT_ERR;
	//convert names of input directories to strings and check that they exist
	int folderN = argc - firstDirNo;
	stringv folders(folderN); 
	for(int argNo = firstDirNo; argNo < argc; argNo++)
	{
		folders[argNo - firstDirNo] = string(argv[argNo]);
		struct stat status;
		if((stat(argv[argNo], &status) != 0) || !(status.st_mode & S_IFDIR))
			throw DIR_ERR;
	}

//1.a) delete all temp files from the previous run and create a directory AGTemp
#ifdef WIN32	//in windows
	CreateDirectory("AGTemp", NULL);
#else // in linux
	system("rm -rf ./AGTemp/");
	system("mkdir ./AGTemp/");
#endif

//2. Set parameters from AGTemp/params.txt from the first directory
	TrainInfo ti;			//set of model parameters in the current directory
	double prevBest;		//best value of performance achieved on the previous run
			
	fstream fparam;
	string paramPathName = folders[0] + "/AGTemp/params.txt";
	fparam.open(paramPathName.c_str(), ios_base::in); 
	string modeStr, metric;
	fparam >> ti.seed >> ti.trainFName >> ti.validFName >> ti.attrFName >> ti.minAlpha >> ti.maxTiGN 
		>> ti.bagN >> modeStr >> metric;	

	//modeStr should be "fast" or "slow" or "layered"	
	if(modeStr.compare("fast") == 0)
		ti.mode = FAST;
	else if(modeStr.compare("slow") == 0)
		ti.mode = SLOW;
	else if(modeStr.compare("layered") == 0)
		ti.mode = LAYERED;
	else
		throw TEMP_ERR;

	//metric should be "roc" or "rms"
	if(metric.compare("rms") == 0)
		ti.rms = true;
	else if(metric.compare("roc") == 0)
		ti.rms = false;
	else
		throw TEMP_ERR;

	if(fparam.fail())
		throw TEMP_ERR;
	fparam.close();
	fparam.clear();

	//read best value of performance on previous run
	fstream fbest;
	double stub;
	double trainV; // number of data points in the train set, need to calculate possible values of alpha
	string fbestPathName = folders[0] + "/AGTemp/best.txt";
	fbest.open(fbestPathName.c_str(), ios_base::in); 
	fbest >> prevBest >> stub >> stub >> stub >> trainV;
	if(fbest.fail())
		throw TEMP_ERR;
	fbest.close();

	int alphaN = getAlphaN(ti.minAlpha, trainV); //number of different alpha values
	int tigNN = getTiGNN(ti.maxTiGN);

	//direction of initialization (1 - up, 0 - right), used in fast mode only
	doublevv dir(tigNN, doublev(alphaN, 0)); 
	//outer array: column (by TiGN)
	//middle array: row	(by alpha)

	//direction of initialization (1 - up, 0 - right), collects average in the slow mode
	doublevv dirStat(tigNN, doublev(alphaN, 0));

	if(ti.mode == FAST)
	{//read part of the directions table from file
		fstream fdir;
		string fdirPathName = folders[0] + "/AGTemp/dir.txt";
		fdir.open(fdirPathName.c_str(), ios_base::in); 
		for(int tigNNo = 0; tigNNo < tigNN; tigNNo++)
			for(int alphaNo = 0; alphaNo < alphaN; alphaNo++)
				fdir >> dir[tigNNo][alphaNo];
		if(fdir.fail())
			throw TEMP_ERR;
		fdir.close();
	}

//3. Read main parameters from all other directories and check that they match

	int allBagN = ti.bagN;
	int lastSeed = ti.seed;
	for(int folderNo = 1; folderNo < folderN; folderNo++)
	{
		TrainInfo extraTI;	//set of model parameters in the additional directory
		
		string fparamPathName = folders[folderNo] + "/AGTemp/params.txt";
		fparam.open(fparamPathName.c_str(), ios_base::in); 
		fparam >> extraTI.seed >> extraTI.trainFName >> extraTI.validFName >> extraTI.attrFName 
			>> extraTI.minAlpha >> extraTI.maxTiGN >> extraTI.bagN;	

		if(fparam.fail())
		{
			clog << fparamPathName << '\n';
			throw TEMP_ERR;
		}
		fparam.close();

		if((ti.minAlpha != extraTI.minAlpha) || (ti.maxTiGN != extraTI.maxTiGN))
		{
		    clog << fparamPathName << '\n';
			throw MERGE_MISMATCH_ERR;
		}
		if(extraTI.seed == ti.seed)
			throw SAME_SEED_ERR;
		if(folderNo == (folderN - 1))
			lastSeed = extraTI.seed;

		allBagN += extraTI.bagN;

		string fdirStatPathName = folders[folderNo] + "/AGTemp/dirstat.txt";
		fstream fdirStat;	
		fdirStat.open("./AGTemp/dirstat.txt", ios_base::in);
		for(int alphaNo = 0; alphaNo < alphaN; alphaNo++)
			for(int tigNNo = 0; tigNNo < tigNN; tigNNo++)
			{
				double ds;
				fdirStat >> ds;
				dirStat[tigNNo][alphaNo] += ds * extraTI.bagN;
			}
	}

//4. Load data
	INDdata data("", ti.validFName.c_str(), "", ti.attrFName.c_str());
	doublev validTar;
	int validN = data.getTargets(validTar, VALID);

	clog << "Alpha = " << ti.minAlpha << "\nN = " << ti.maxTiGN << "\n" 
		<< allBagN << " bagging iterations\n";
	if(ti.mode == FAST)
		clog << "fast mode\n\n";
	else if(ti.mode == SLOW)
		clog << "slow mode\n\n";
	else //if(ti.mode == LAYERED)
		clog << "layered mode\n\n";

	//5. Initialize some internal process variables

	//surfaces of performance values for validation set. 
	//Always calculate rms (for convergence analysis), if needed, calculate roc
	doublevvv rmsV(tigNN, doublevv(alphaN, doublev(folderN, 0))); 
	doublevvv rocV;
	if(!ti.rms)
		rocV.resize(tigNN, doublevv(alphaN, doublev(folderN, 0))); 
	//outer array: column (by TiGN)
	//middle array: row (by alpha)
	//inner array: bagging iterations. Performance is kept for all iterations to create bagging curves

	//sums of predictions for each data point (raw material to calculate performance)
	doublevvv predsumsV(tigNN, doublevv(alphaN, doublev(validN, 0)));
	//outer array: column (by TiGN)
	//middle array: row	(by alpha)
	//inner array: data points in the validation set
	

//6. Read and merge models from the directories
	int startAlphaNo = getAlphaN(startAlpha, trainV) - 1; 
	int startTiGNNo = getTiGNN(startTiGN) - 1;

	for(int alphaNo = startAlphaNo; alphaNo < alphaN; alphaNo++)
	{
		double alpha;
		if(alphaNo < alphaN - 1)
			alpha = alphaVal(alphaNo);
		else	//this is a special case because minAlpha can be zero
			alpha = ti.minAlpha;

		cout << "Merging predictions with alpha = " << alpha << endl;

		for(int tigNNo = startTiGNNo; tigNNo < tigNN; tigNNo++) 
		{
			int tigN = tigVal(tigNNo);	//number of trees in the current grove

			//temp file in the extra directory that keeps models corresponding to alpha and tigN
			string prefix = string("/AGTemp/ag.a.") 
								+ alphaToStr(alpha)
								+ ".n." 
								+ itoa(tigN, 10);
			string predsFName = prefix + ".preds.txt";

			for(int folderNo = 0; folderNo < folderN; folderNo++)
			{
				string inPredsFName = folders[folderNo] + predsFName;
				fstream finpreds((inPredsFName).c_str(), ios_base::in);
				if(finpreds.fail())
				{
				    clog << inPredsFName << '\n';
					throw TEMP_ERR;
				}
				//generate predictions and performance for validation set
				doublev predictions(validN);
				for(int itemNo = 0; itemNo < validN; itemNo++)
				{
					double sinpred;
					finpreds >> sinpred;

					predsumsV[tigNNo][alphaNo][itemNo] += sinpred;//extraGrove.predict(itemNo, VALID);
					predictions[itemNo] = predsumsV[tigNNo][alphaNo][itemNo] / (folderNo + 1);
				}
				if(folderNo == folderN - 1)
				{
					fstream fpreds((string(".") + predsFName).c_str(), ios_base::out);
					for(int itemNo = 0; itemNo < validN; itemNo++)
						fpreds << predictions[itemNo] << endl;
					fpreds.close();
				}

				rmsV[tigNNo][alphaNo][folderNo] = rmse(predictions, validTar);
				if(!ti.rms)
					rocV[tigNNo][alphaNo][folderNo] = roc(predictions, validTar);

				finpreds.close();
			}//end for(int folderNo = 0; folderNo < folderN; folderNo++)
		}//end for(int tigNNo = 0; tigNNo < tigNN; tigNNo++) 
	}//end for(int alphaNo = 0; alphaNo < alphaN; alphaNo++)

	//4. Output
	ti.bagN = folderN;
	ti.seed = lastSeed;
	if(ti.rms)
		trainOut(ti, dir, rmsV, rmsV, predsumsV, trainV, dirStat, startAlphaNo, startTiGNNo);
	else
		trainOut(ti, dir, rmsV, rocV, predsumsV, trainV, dirStat, startAlphaNo, startTiGNNo);

	if(folderN != allBagN)
		clog << "Warning: bagging curve and -b recommendations could not be calculated correctly "
			<< "in this mode. Each visible bagging step corresponds to several real steps.\n";

	}catch(TE_ERROR err){
		ErrLogStream errlog;
		switch(err)
		{
			case TREE_LOAD_ERR:
				errlog << "Error: temporary files from previous runs of train/expand "
					<< "are corrupted.\n";
				break;
			case MODEL_ATTR_MISMATCH_ERR:
				errlog << "Error: either temporary files from previous runs of train/expand "
					<< "or attribute file are corrupted.\n";
				break;
			default:
				te_errMsg((TE_ERROR)err);
		}
		return 1;

	}catch(AG_ERROR err){
		ErrLogStream errlog;
		switch(err)
		{
			case TEMP_ERR:
				errlog << "Error: temporary files from previous runs of train/expand "
					<< "are missing or corrupted.\n";
				break;
			case INPUT_ERR:
				errlog << "Usage: ag_mergelight [-n _start_N_value_] [-a _start_alpha_value_] "
					<< "-d _directory1_ _directory2_ [_directory3_] [_directory4_] ...\n";
				break;
			case DIR_ERR:
				errlog << "Error: one of input directories does not exist.\n";
				break;
			case MERGE_MISMATCH_ERR:
				errlog << "Error: model parameters in input directories do not match.\n";
				break;
			case SAME_SEED_ERR:
				errlog << "Error: attempting to merge models built with the same random seed.\n";
				break;
			default:
				throw err;
		}
		return 1;
	}catch(exception &e){
		ErrLogStream errlog;
		string errstr(e.what());
		errlog << "Error: " << errstr << "\n";
		return 1;
	}catch(...){
		string errstr = strerror(errno);
		ErrLogStream errlog;
		errlog << "Error: " << errstr << "\n";
		return 1;
	}
	return 0;
}
