/*
Author: Aaron Armbruster
Date:   2012-06-01
Email:  armbrusa@umich.edu
Description: 

Compute statistical significance with profile likelihood test stat. 
Option for uncapped test stat is added (doUncap), as well as an option
to choose which mu value to profile observed data at before generating expected

*/




#include "RooWorkspace.h"
#include "RooStats/ModelConfig.h"
#include "RooDataSet.h"
#include "RooMinimizerFcn.h"
#include "RooNLLVar.h"
#include "RooRealVar.h"
#include "RooSimultaneous.h"
#include "RooCategory.h"
#include "RooMinimizer.h"
#include "Math/MinimizerOptions.h"

#include "TH1D.h"

#include "TStopwatch.h"

//#include "macros/makeAsimovData.C"
//#include "macros/makeData.C"

#include "TFile.h"
#include "TMath.h"

#include <sstream>
#include <iostream>
#include <iomanip>

using namespace std;
using namespace RooFit;
using namespace RooStats;

RooDataSet* makeAsimovData(ModelConfig* mc, bool doConditional, RooWorkspace* w, RooNLLVar* conditioning_nll, double mu_val, string* mu_str, string* mu_prof_str, double mu_val_profile, bool doFit, double mu_injection = -1);
int minimize(RooNLLVar* nll, RooWorkspace* combWS = NULL);
void runSig(const char* inFileName,
	    const char* wsName = "combined",  ///combined",
	    const char* modelConfigName = "ModelConfig",
	    const char* dataName = "obsData",
	    const char* asimov1DataName = "asimovData_1",
	    const char* conditional1Snapshot = "conditionalGlobs_1",
	    const char* nominalSnapshot = "nominalGlobs",
	    string smass = "110",
	    string folder = "test",
      bool doBlind = false)             // in case your analysis is blinded
{
  double mass;
  stringstream massStr;
  massStr << smass;
  massStr >> mass;



  double mu_profile_value = 1; // mu value to profile the obs data at before generating the expected
  bool doConditional      = 1 && !doBlind; // do conditional expected data
  bool remakeData         = 0; // handle unphysical pdf cases in H->ZZ->4l
  bool doUncap            = 1; // uncap p0
  bool doInj              = 1; // setup the poi for injection study (zero is faster if you're not)
  bool doObs              = 1 && !doBlind; // compute median significance
  bool doMedian           = 1; // compute observed significance

  TStopwatch timer;
  timer.Start();

  TFile f(inFileName);
  RooWorkspace* ws = (RooWorkspace*)f.Get(wsName);
  if (!ws)
  {
    cout << "ERROR::Workspace: " << wsName << " doesn't exist!" << endl;
    return;
  }
  ModelConfig* mc = (ModelConfig*)ws->obj(modelConfigName);
  if (!mc)
  {
    cout << "ERROR::ModelConfig: " << modelConfigName << " doesn't exist!" << endl;
    return;
  }
  RooDataSet* data = (RooDataSet*)ws->data(dataName);
  if (!data)
  {
    cout << "ERROR::Dataset: " << dataName << " doesn't exist!" << endl;
    return;
  }

  RooArgSet nuis(mc->GetNuisanceParameters() ? *mc->GetNuisanceParameters() : RooArgSet());
  nuis.Print("v");
  int numberOfNP=nuis.getSize();

  //RooNLLVar::SetIgnoreZeroEntries(1);
  ROOT::Math::MinimizerOptions::SetDefaultMinimizer("Minuit2");
  ROOT::Math::MinimizerOptions::SetDefaultStrategy(1);
  ROOT::Math::MinimizerOptions::SetDefaultPrintLevel(1);
  cout << "Setting max function calls" << endl;
  //ROOT::Math::MinimizerOptions::SetDefaultMaxFunctionCalls(20000);
  //RooMinimizer::SetMaxFunctionCalls(10000);

  ws->loadSnapshot("conditionalNuis_0");

  RooRealVar* mu = (RooRealVar*)mc->GetParametersOfInterest()->first();
  mu->setMin(-50);
  double mu_init = mu->getVal();

  RooAbsPdf* pdf = mc->GetPdf();
//   if (string(pdf->ClassName()) == "RooSimultaneous" && remakeData)
//   {
//     RooSimultaneous* simPdf = (RooSimultaneous*)pdf;
//     double min_mu;
//     data = makeData(data, simPdf, mc->GetObservables(), mu, mass, min_mu);
//   }




  string condSnapshot(conditional1Snapshot);
  RooArgSet nuis_tmp2 =(mc->GetNuisanceParameters() ? *mc->GetNuisanceParameters() : RooArgSet());
  
  TString nCPU_str = getenv("NCORE");
  int nCPU = nCPU_str.Atoi();

  RooNLLVar* obs_nll = doObs ? (RooNLLVar*)pdf->createNLL(*data, Constrain(nuis_tmp2), Offset(1), Optimize(2), NumCPU(nCPU,3)) : NULL;
  RooDataSet* asimovData1 = (RooDataSet*)ws->data(asimov1DataName);
  RooRealVar* emb = (RooRealVar*)nuis.find("ATLAS_EMB");
  if (!asimovData1 || (string(inFileName).find("ic10") != string::npos && emb))
  {
    if (emb) emb->setVal(0.7);
    cout << "Asimov data doesn't exist! Please, allow me to build one for you..." << endl;
    string mu_str, mu_prof_str;
    asimovData1 = makeAsimovData(mc, doConditional, ws, obs_nll, 1, &mu_str, &mu_prof_str, mu_profile_value, true);
    condSnapshot="conditionalGlobs"+mu_prof_str;

    //makeAsimovData(mc, true, ws, mc->GetPdf(), data, 0);
    //ws->Print();
    //asimovData1 = (RooDataSet*)ws->data("asimovData_1");
  }
  
  if (!doUncap) mu->setRange(0, 40);
  else mu->setRange(-40, 40);






  pdf = mc->GetPdf();


  RooArgSet nuis_tmp1 = nuis;
  RooNLLVar* asimov_nll = (RooNLLVar*)pdf->createNLL(*asimovData1, Constrain(nuis_tmp1), Offset(1), Optimize(2), NumCPU(nCPU,3));



    
//do asimov
  mu->setVal(1);
  mu->setConstant(1);

  int status,sign;
  double med_sig=0,obs_sig=0,inj_sig=0,asimov_q0=0,obs_q0=0,inj_q0=0;

  if (doMedian)
  {
    ws->loadSnapshot(condSnapshot.c_str());
    ws->loadSnapshot("conditionalNuis_1");
    mc->GetGlobalObservables()->Print("v");
    mu->setVal(0);
    mu->setConstant(1);

    // kick NP (only if we have NP's. Not done if statOnly fit)
    if (numberOfNP>0){
	const RooArgSet *np = mc->GetNuisanceParameters();
	RooRealVar* var = (RooRealVar*)(np->first());
	var->setVal(var->getVal() + 0.1);
    }


    status = minimize(asimov_nll, ws);
    if (status < 0) 
    {
      cout << "Retrying with conditional snapshot at mu=1" << endl;
      ws->loadSnapshot("conditionalNuis_0");
      status = minimize(asimov_nll, ws);
      if (status >= 0) cout << "Success!" << endl;
    }
    double asimov_nll_cond = asimov_nll->getVal();

    mu->setVal(1);
    ws->loadSnapshot("conditionalNuis_1");
    // kick NP (only if we have NP's. Not done if statOnly fit)
    if (numberOfNP>0){
	const RooArgSet *np2 = mc->GetNuisanceParameters();
	RooRealVar*var = (RooRealVar*)(np2->first());
	var->setVal(var->getVal() + 0.1);
    }

    status = minimize(asimov_nll, ws);
    if (status < 0) 
    {
      cout << "Retrying with conditional snapshot at mu=1" << endl;
      ws->loadSnapshot("conditionalNuis_0");
      status = minimize(asimov_nll, ws);
      if (status >= 0) cout << "Success!" << endl;
    }

    double asimov_nll_min = asimov_nll->getVal();
    asimov_q0 = 2*(asimov_nll_cond - asimov_nll_min);
    if (doUncap && mu->getVal() < 0) asimov_q0 = -asimov_q0;

    sign = int(asimov_q0 != 0 ? asimov_q0/fabs(asimov_q0) : 0);
    med_sig = sign*sqrt(fabs(asimov_q0));

    ws->loadSnapshot(nominalSnapshot);
  }







  if (doObs)
  {

    ws->loadSnapshot("conditionalNuis_0");
    mu->setVal(0);
    mu->setConstant(1);
    status = minimize(obs_nll, ws);
    if (status < 0) 
    {
      cout << "Retrying with conditional snapshot at mu=1" << endl;
      ws->loadSnapshot("conditionalNuis_0");
      status = minimize(obs_nll, ws);
      if (status >= 0) cout << "Success!" << endl;
    }
    double obs_nll_cond = obs_nll->getVal();



    //ws->loadSnapshot("ucmles");
    mu->setConstant(0);
    status = minimize(obs_nll, ws);
    if (status < 0) 
    {
      cout << "Retrying with conditional snapshot at mu=1" << endl;
      ws->loadSnapshot("conditionalNuis_0");
      status = minimize(obs_nll, ws);
      if (status >= 0) cout << "Success!" << endl;
    }

    double obs_nll_min = obs_nll->getVal();



    obs_q0 = 2*(obs_nll_cond - obs_nll_min);
    if (doUncap && mu->getVal() < 0) obs_q0 = -obs_q0;

    sign = int(obs_q0 == 0 ? 0 : obs_q0 / fabs(obs_q0));
    if ( !doUncap && ((obs_q0 < 0 && obs_q0 > -0.1) || mu->getVal() < 0.001)) obs_sig = 0; 
    else obs_sig = sign*sqrt(fabs(obs_q0));
  }


  if (doInj)
  {

    string mu_str, mu_prof_str;
    double mu_inj = 1.;
    if ( ws->var("ATLAS_norm_muInjection") ) {
       mu_inj = ws->var("ATLAS_norm_muInjection")->getVal();
    } else {
       mu_inj = mu_init; // for the mass point at the inj
    }
    RooDataSet* injData1 = makeAsimovData(mc, doConditional, ws, obs_nll, 0, &mu_str, &mu_prof_str, 1, true, mu_inj);
    string globObsSnapName = "conditionalGlobs"+mu_prof_str;
    ws->loadSnapshot(globObsSnapName.c_str());
    RooNLLVar* inj_nll = (RooNLLVar*)pdf->createNLL(*injData1, Constrain(nuis_tmp2), Offset(1), Optimize(2), NumCPU(nCPU,3));

    ws->loadSnapshot("conditionalNuis_0");
    mu->setVal(0);
    mu->setConstant(1);
    status = minimize(inj_nll, ws);
    if (status < 0) 
    {
      cout << "Retrying with conditional snapshot at mu=1" << endl;
      ws->loadSnapshot("conditionalNuis_0");
      status = minimize(inj_nll, ws);
      if (status >= 0) cout << "Success!" << endl;
    }
    double inj_nll_cond = inj_nll->getVal();



    //ws->loadSnapshot("ucmles");
    mu->setConstant(0);
    status = minimize(inj_nll, ws);
    if (status < 0) 
    {
      cout << "Retrying with conditional snapshot at mu=1" << endl;
      ws->loadSnapshot("conditionalNuis_0");
      status = minimize(inj_nll, ws);
      if (status >= 0) cout << "Success!" << endl;
    }

    double inj_nll_min = inj_nll->getVal();



    inj_q0 = 2*(inj_nll_cond - inj_nll_min);
    if (doUncap && mu->getVal() < 0) inj_q0 = -inj_q0;

    sign = int(inj_q0 == 0 ? 0 : inj_q0 / fabs(inj_q0));
    if (!doUncap && ((inj_q0 < 0 && inj_q0 > -0.1) || mu->getVal() < 0.001)) inj_sig = 0; 
    else inj_sig = sign*sqrt(fabs(inj_q0));

  }



  ///Chiara: calculate p0 from gaussian significance
  float obs_pValue = 1, med_pValue = 1, inj_pValue = 1;
  if(obs_sig!=0) obs_pValue = (1-TMath::Erf(obs_sig/sqrt(2)))/2;
  if(med_sig!=0) med_pValue = (1-TMath::Erf(med_sig/sqrt(2)))/2;
  if(inj_sig!=0) inj_pValue = (1-TMath::Erf(inj_sig/sqrt(2)))/2;

  ///cout << "obs: " << obs_sig << endl;

  cout << "Observed significance: " << obs_sig << endl;
  cout << "Observed pValue: " << obs_pValue << endl;
  if (med_sig)
  {
    cout << "Median test stat val: " << asimov_q0 << endl;
    cout << "Median significance:   " << med_sig << endl;
    cout << "Median pValue: " << med_pValue << endl;
  }
  if (inj_sig)
  {
    cout << "Injected test stat val: " << inj_q0 << endl;
    cout << "Injected significance:   " << inj_sig << endl;
    cout << "Injected pValue: " << inj_pValue << endl;
  }


  f.Close();

  stringstream fileName;
  fileName << "root-files/" << folder << "/" << mass << ".root";
  system(("mkdir -vp root-files/" + folder).c_str());
  TFile f2(fileName.str().c_str(),"recreate");

  TH1D* h_hypo = new TH1D("hypo","hypo",6,0,6);
  h_hypo->SetBinContent(1, obs_sig);
  h_hypo->SetBinContent(2, med_sig);
  h_hypo->SetBinContent(3, inj_sig);
  h_hypo->SetBinContent(4, obs_pValue);
  h_hypo->SetBinContent(5, med_pValue);
  h_hypo->SetBinContent(6, inj_pValue);
  h_hypo->GetXaxis()->SetBinLabel(1, "Observed sig");
  h_hypo->GetXaxis()->SetBinLabel(2, "Expected sig");
  h_hypo->GetXaxis()->SetBinLabel(3, "Injected sig");
  h_hypo->GetXaxis()->SetBinLabel(4, "Observed p0");
  h_hypo->GetXaxis()->SetBinLabel(5, "Expected p0");
  h_hypo->GetXaxis()->SetBinLabel(6, "Injected p0");


  f2.Write();
  f2.Close();

  timer.Stop();
  timer.Print();

}


int minimize(RooNLLVar* nll, RooWorkspace* combWS)
{
  bool const_test = 0;

  vector<string> const_vars;
  const_vars.push_back("alpha_ATLAS_JES_NoWC_llqq");
  const_vars.push_back("alpha_ATLAS_ZBB_PTW_NoWC_llqq");
  const_vars.push_back("alpha_ATLAS_ZCR_llqqNoWC_llqq");

  int nrConst = const_vars.size();

  if (const_test)
  {
    for (int i=0;i<nrConst;i++)
    {
      RooRealVar* const_var = combWS->var(const_vars[i].c_str());
      const_var->setConstant(1);
    }
  }




  int printLevel = ROOT::Math::MinimizerOptions::DefaultPrintLevel();
  RooFit::MsgLevel msglevel = RooMsgService::instance().globalKillBelow();
  if (printLevel < 0) RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

  int strat = ROOT::Math::MinimizerOptions::DefaultStrategy();
  RooMinimizer minim(*nll);
  minim.setStrategy(strat);
  minim.setPrintLevel(printLevel);


  int status = minim.minimize(ROOT::Math::MinimizerOptions::DefaultMinimizerType().c_str(), ROOT::Math::MinimizerOptions::DefaultMinimizerAlgo().c_str());


  if (status != 0 && status != 1 && strat < 2)
  {
    strat++;
    cout << "Fit failed with status " << status << ". Retrying with strategy " << strat << endl;
    minim.setStrategy(strat);
    status = minim.minimize(ROOT::Math::MinimizerOptions::DefaultMinimizerType().c_str(), ROOT::Math::MinimizerOptions::DefaultMinimizerAlgo().c_str());
  }

  if (status != 0 && status != 1 && strat < 2)
  {
    strat++;
    cout << "Fit failed with status " << status << ". Retrying with strategy " << strat << endl;
    minim.setStrategy(strat);
    status = minim.minimize(ROOT::Math::MinimizerOptions::DefaultMinimizerType().c_str(), ROOT::Math::MinimizerOptions::DefaultMinimizerAlgo().c_str());
  }

  if (status != 0 && status != 1)
  {
    cout << "Fit failed with status " << status << endl;
  }

//   if (status != 0 && status != 1)
//   {
//     cout << "Fit failed for mu = " << mu->getVal() << " with status " << status << ". Retrying with pdf->fitTo()" << endl;
//     combPdf->fitTo(*combData,Hesse(false),Minos(false),PrintLevel(0),Extended(), Constrain(nuiSet_tmp));
//   }
  if (printLevel < 0) RooMsgService::instance().setGlobalKillBelow(msglevel);


  if (const_test)
  {
    for (int i=0;i<nrConst;i++)
    {
      RooRealVar* const_var = combWS->var(const_vars[i].c_str());
      const_var->setConstant(0);
    }
  }


  return status;
}


//put very small data entries in a binned dataset to avoid unphysical pdfs, specifically for H->ZZ->4l
RooDataSet* makeData(RooDataSet* orig, RooSimultaneous* simPdf, const RooArgSet* observables, RooRealVar* firstPOI, double mass, double& mu_min)
{

  double max_soverb = 0;

  mu_min = -10e9;


  map<string, RooDataSet*> data_map;
  firstPOI->setVal(0);
  RooCategory* cat = (RooCategory*)&simPdf->indexCat();
  TList* datalist = orig->split(*(RooAbsCategory*)cat, true);
  TIterator* dataItr = datalist->MakeIterator();
  RooAbsData* ds;
  RooRealVar* weightVar = new RooRealVar("weightVar","weightVar",1);
  while ((ds = (RooAbsData*)dataItr->Next()))
  {
    string typeName(ds->GetName());
    cat->setLabel(typeName.c_str());
    RooAbsPdf* pdf = simPdf->getPdf(typeName.c_str());
    cout << "pdf: " << pdf << endl;
    RooArgSet* obs = pdf->getObservables(observables);
    cout << "obs: " << obs << endl;

    RooArgSet obsAndWeight(*obs, *weightVar);
    obsAndWeight.add(*cat);
    stringstream datasetName;
    datasetName << "newData_" << typeName;
    RooDataSet* thisData = new RooDataSet(datasetName.str().c_str(),datasetName.str().c_str(), obsAndWeight, WeightVar(*weightVar));

    RooRealVar* firstObs = (RooRealVar*)obs->first();
    //int ibin = 0;
    int nrEntries = ds->numEntries();
    for (int ib=0;ib<nrEntries;ib++)
    {
      const RooArgSet* event = ds->get(ib);
      const RooRealVar* thisObs = (RooRealVar*)event->find(firstObs->GetName());
      firstObs->setVal(thisObs->getVal());

      firstPOI->setVal(0);
      double b = pdf->expectedEvents(*firstObs)*pdf->getVal(obs);
      firstPOI->setVal(1);
      double s = pdf->expectedEvents(*firstObs)*pdf->getVal(obs) - b;

      if (s > 0)
      {
	mu_min = max(mu_min, -b/s);
	double soverb = s/b;
	if (soverb > max_soverb)
	{
	  max_soverb = soverb;
	  cout << "Found new max s/b: " << soverb << " in pdf " << pdf->GetName() << " at m = " << thisObs->getVal() << endl;
	}
      }

      
      
      //cout << "expected s = " << s << ", b = " << b << endl;
      //cout << "nexp = " << nexp << endl;
//       if (s < 0) 
//       {
// 	cout << "expecting negative s at m=" << firstObs->getVal() << endl;
// 	continue;
//       }
      if (b == 0 && s != 0)
      {
	cout << "Expecting non-zero signal and zero bg at m=" << firstObs->getVal() << " in pdf " << pdf->GetName() << endl;
      }
      if (s+b <= 0) 
      {
	cout << "expecting zero" << endl;
	continue;
      }


      double weight = ds->weight();
      if ((typeName.find("ATLAS_H_4mu") != string::npos || 
	   typeName.find("ATLAS_H_4e") != string::npos ||
	   typeName.find("ATLAS_H_2mu2e") != string::npos ||
	   typeName.find("ATLAS_H_2e2mu") != string::npos) && fabs(firstObs->getVal() - mass) < 10 && weight == 0)
      {
	cout << "adding event: " << firstObs->getVal() << endl;
	thisData->add(*event, pow(10., -9.));
      }
      else
      {
	//weight = max(pow(10.0, -9), weight);
	thisData->add(*event, weight);
      }
    }



    data_map[string(ds->GetName())] = (RooDataSet*)thisData;
  }

  
  RooDataSet* newData = new RooDataSet("newData","newData",RooArgSet(*observables, *weightVar), 
				       Index(*cat), Import(data_map), WeightVar(*weightVar));

  orig->Print();
  newData->Print();
  //newData->tree()->Scan("*");
  return newData;

}

void unfoldConstraints(RooArgSet& initial, RooArgSet& final, RooArgSet& obs, RooArgSet& nuis, int& counter)
{
  if (counter > 50)
  {
    cout << "ERROR::Couldn't unfold constraints!" << endl;
    cout << "Initial: " << endl;
    initial.Print("v");
    cout << endl;
    cout << "Final: " << endl;
    final.Print("v");
    exit(1);
  }
  TIterator* itr = initial.createIterator();
  RooAbsPdf* pdf;
  while ((pdf = (RooAbsPdf*)itr->Next()))
  {
    RooArgSet nuis_tmp = nuis;
    RooArgSet constraint_set(*pdf->getAllConstraints(obs, nuis_tmp, false));
    //if (constraint_set.getSize() > 1)
    //{
    string className(pdf->ClassName());
    if (className != "RooGaussian" && className != "RooLognormal" && className != "RooGamma" && className != "RooPoisson" && className != "RooBifurGauss")
    {
      counter++;
      unfoldConstraints(constraint_set, final, obs, nuis, counter);
    }
    else
    {
      final.add(*pdf);
    }
  }
  delete itr;
}

RooDataSet* makeAsimovData(ModelConfig* mc, bool doConditional, RooWorkspace* w, RooNLLVar* conditioning_nll, double mu_val, string* mu_str, string* mu_prof_str, double mu_val_profile, bool doFit, double mu_injection)
{
  if (mu_val_profile == -999) mu_val_profile = mu_val;


  cout << "Creating asimov data at mu = " << mu_val << ", profiling at mu = " << mu_val_profile << endl;

  //ROOT::Math::MinimizerOptions::SetDefaultMinimizer("Minuit2");
  //int strat = ROOT::Math::MinimizerOptions::SetDefaultStrategy(0);
  //int printLevel = ROOT::Math::MinimizerOptions::DefaultPrintLevel();
  //ROOT::Math::MinimizerOptions::SetDefaultPrintLevel(-1);
  //RooMinuit::SetMaxIterations(10000);
  //RooMinimizer::SetMaxFunctionCalls(10000);

////////////////////
//make asimov data//
////////////////////
  RooAbsPdf* combPdf = mc->GetPdf();

  int _printLevel = 0;

  stringstream muStr;
  muStr << setprecision(5);
  muStr << "_" << mu_val;
  if (mu_str) *mu_str = muStr.str();

  stringstream muStrProf;
  muStrProf << setprecision(5);
  muStrProf << "_" << mu_val_profile;
  if (mu_prof_str) *mu_prof_str = muStrProf.str();

  RooRealVar* mu = (RooRealVar*)mc->GetParametersOfInterest()->first();//w->var("mu");
  mu->setVal(mu_val);

  RooArgSet mc_obs = *mc->GetObservables();
  RooArgSet mc_globs = *mc->GetGlobalObservables();
  RooArgSet mc_nuis = (mc->GetNuisanceParameters() ? *mc->GetNuisanceParameters() : RooArgSet()); 
//pair the nuisance parameter to the global observable
  RooArgSet mc_nuis_tmp = mc_nuis;
  RooArgList nui_list("ordered_nuis");
  RooArgList glob_list("ordered_globs");
  RooArgSet constraint_set_tmp(*combPdf->getAllConstraints(mc_obs, mc_nuis_tmp, false));
  RooArgSet constraint_set;
  int counter_tmp = 0;
  unfoldConstraints(constraint_set_tmp, constraint_set, mc_obs, mc_nuis_tmp, counter_tmp);

  TIterator* cIter = constraint_set.createIterator();
  RooAbsArg* arg;
  while ((arg = (RooAbsArg*)cIter->Next()))
  {
    RooAbsPdf* pdf = (RooAbsPdf*)arg;
    if (!pdf) continue;
//     cout << "Printing pdf" << endl;
//     pdf->Print();
//     cout << "Done" << endl;
    TIterator* nIter = mc_nuis.createIterator();
    RooRealVar* thisNui = NULL;
    RooAbsArg* nui_arg;
    while ((nui_arg = (RooAbsArg*)nIter->Next()))
    {
      if (pdf->dependsOn(*nui_arg))
      {
	thisNui = (RooRealVar*)nui_arg;
	break;
      }
    }
    delete nIter;

    //RooRealVar* thisNui = (RooRealVar*)pdf->getObservables();


//need this incase the observable isn't fundamental. 
//in this case, see which variable is dependent on the nuisance parameter and use that.
    RooArgSet* components = pdf->getComponents();
//     cout << "\nPrinting components" << endl;
//     components->Print();
//     cout << "Done" << endl;
    components->remove(*pdf);
    if (components->getSize())
    {
      TIterator* itr1 = components->createIterator();
      RooAbsArg* arg1;
      while ((arg1 = (RooAbsArg*)itr1->Next()))
      {
	TIterator* itr2 = components->createIterator();
	RooAbsArg* arg2;
	while ((arg2 = (RooAbsArg*)itr2->Next()))
	{
	  if (arg1 == arg2) continue;
	  if (arg2->dependsOn(*arg1))
	  {
	    components->remove(*arg1);
	  }
	}
	delete itr2;
      }
      delete itr1;
    }
    if (components->getSize() > 1)
    {
      cout << "ERROR::Couldn't isolate proper nuisance parameter" << endl;
      return NULL;
    }
    else if (components->getSize() == 1)
    {
      thisNui = (RooRealVar*)components->first();
    }



    TIterator* gIter = mc_globs.createIterator();
    RooRealVar* thisGlob = NULL;
    RooAbsArg* glob_arg;
    while ((glob_arg = (RooAbsArg*)gIter->Next()))
    {
      if (pdf->dependsOn(*glob_arg))
      {
	thisGlob = (RooRealVar*)glob_arg;
	break;
      }
    }
    delete gIter;

    if (!thisNui || !thisGlob)
    {
      cout << "WARNING::Couldn't find nui or glob for constraint: " << pdf->GetName() << endl;
      //return;
      continue;
    }

    if (_printLevel >= 1) cout << "Pairing nui: " << thisNui->GetName() << ", with glob: " << thisGlob->GetName() << ", from constraint: " << pdf->GetName() << endl;

    nui_list.add(*thisNui);
    glob_list.add(*thisGlob);

//     cout << "\nPrinting Nui/glob" << endl;
//     thisNui->Print();
//     cout << "Done nui" << endl;
//     thisGlob->Print();
//     cout << "Done glob" << endl;
  }
  delete cIter;




//save the snapshots of nominal parameters, but only if they're not already saved
  w->saveSnapshot("tmpGlobs",*mc->GetGlobalObservables());
  w->saveSnapshot("tmpNuis",(mc->GetNuisanceParameters() ? *mc->GetNuisanceParameters() : RooArgSet()));

  if (!w->loadSnapshot("nominalGlobs"))
  {
    cout << "nominalGlobs doesn't exist. Saving snapshot." << endl;
    w->saveSnapshot("nominalGlobs",*mc->GetGlobalObservables());
  }
  else w->loadSnapshot("tmpGlobs");
  if (!w->loadSnapshot("nominalNuis"))
  {
    cout << "nominalNuis doesn't exist. Saving snapshot." << endl;
    w->saveSnapshot("nominalNuis",(mc->GetNuisanceParameters() ? *mc->GetNuisanceParameters() : RooArgSet()));
  }
  else w->loadSnapshot("tmpNuis");

  RooArgSet nuiSet_tmp(nui_list);

  mu->setVal(mu_val_profile);
  mu->setConstant(1);

  //int status = 0;
  if (doConditional && doFit)
  {
    minimize(conditioning_nll);
    // cout << "Using globs for minimization" << endl;
    // mc->GetGlobalObservables()->Print("v");
    // cout << "Starting minimization.." << endl;
    // RooAbsReal* nll;
    // if (!(nll = map_data_nll[combData])) nll = combPdf->createNLL(*combData, RooFit::Constrain(nuiSet_tmp));
    // RooMinimizer minim(*nll);
    // minim.setStrategy(0);
    // minim.setPrintLevel(1);
    // status = minim.minimize(ROOT::Math::MinimizerOptions::DefaultMinimizerType().c_str(), ROOT::Math::MinimizerOptions::DefaultMinimizerAlgo().c_str());
    // if (status != 0)
    // {
    //   cout << "Fit failed for mu = " << mu->getVal() << " with status " << status << endl;
    // }
    // cout << "Done" << endl;

    //combPdf->fitTo(*combData,Hesse(false),Minos(false),PrintLevel(0),Extended(), Constrain(nuiSet_tmp));
  }
  mu->setConstant(0);
  mu->setVal(mu_val);



//loop over the nui/glob list, grab the corresponding variable from the tmp ws, and set the glob to the value of the nui
  int nrNuis = nui_list.getSize();
  if (nrNuis != glob_list.getSize())
  {
    cout << "ERROR::nui_list.getSize() != glob_list.getSize()!" << endl;
    return NULL;
  }

  for (int i=0;i<nrNuis;i++)
  {
    RooRealVar* nui = (RooRealVar*)nui_list.at(i);
    RooRealVar* glob = (RooRealVar*)glob_list.at(i);

    //cout << "nui: " << nui << ", glob: " << glob << endl;
    //cout << "Setting glob: " << glob->GetName() << ", which had previous val: " << glob->getVal() << ", to conditional val: " << nui->getVal() << endl;

    glob->setVal(nui->getVal());
  }

//save the snapshots of conditional parameters
  cout << "Saving conditional snapshots" << endl;
  cout << "Glob snapshot name = " << "conditionalGlobs"+muStrProf.str() << endl;
  cout << "Nuis snapshot name = " << "conditionalNuis"+muStrProf.str() << endl;
  w->saveSnapshot(("conditionalGlobs"+muStrProf.str()).c_str(),*mc->GetGlobalObservables());
  w->saveSnapshot(("conditionalNuis" +muStrProf.str()).c_str(),(mc->GetNuisanceParameters() ? *mc->GetNuisanceParameters() : RooArgSet()));
  if (!doConditional)
  {
    w->loadSnapshot("nominalGlobs");
    w->loadSnapshot("nominalNuis");
  }

  if (_printLevel >= 1) cout << "Making asimov" << endl;
//make the asimov data (snipped from Kyle)
  mu->setVal(mu_val);

  if(mu_injection > 0) {
    RooRealVar* norm_injection = w->var("ATLAS_norm_muInjection");
    if(norm_injection) {
      norm_injection->setVal(mu_injection);
    }
    else {
      mu->setVal(mu_injection);
    }
  }

  int iFrame=0;

  const char* weightName="weightVar";
  RooArgSet obsAndWeight;
  //cout << "adding obs" << endl;
  obsAndWeight.add(*mc->GetObservables());
  //cout << "adding weight" << endl;

  RooRealVar* weightVar = NULL;
  if (!(weightVar = w->var(weightName)))
  {
    w->import(*(new RooRealVar(weightName, weightName, 1,0,10000000)));
    weightVar = w->var(weightName);
  }
  //cout << "weightVar: " << weightVar << endl;
  obsAndWeight.add(*w->var(weightName));

  //cout << "defining set" << endl;
  w->defineSet("obsAndWeight",obsAndWeight);


  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  // MAKE ASIMOV DATA FOR OBSERVABLES

  // dummy var can just have one bin since it's a dummy
  //if(w->var("ATLAS_dummyX"))  w->var("ATLAS_dummyX")->setBins(1);

  //cout <<" check expectedData by category"<<endl;
  //RooDataSet* simData=NULL;
  RooSimultaneous* simPdf = dynamic_cast<RooSimultaneous*>(mc->GetPdf());

  RooDataSet* asimovData;
  if (!simPdf)
  {
    // Get pdf associated with state from simpdf
    RooAbsPdf* pdftmp = mc->GetPdf();//simPdf->getPdf(channelCat->getLabel()) ;
	
    // Generate observables defined by the pdf associated with this state
    RooArgSet* obstmp = pdftmp->getObservables(*mc->GetObservables()) ;

    if (_printLevel >= 1)
    {
      obstmp->Print();
    }

    asimovData = new RooDataSet(("asimovData"+muStr.str()).c_str(),("asimovData"+muStr.str()).c_str(),RooArgSet(obsAndWeight),WeightVar(*weightVar));

    RooRealVar* thisObs = ((RooRealVar*)obstmp->first());
    double expectedEvents = pdftmp->expectedEvents(*obstmp);
    double thisNorm = 0;
    for(int jj=0; jj<thisObs->numBins(); ++jj){
      thisObs->setBin(jj);

      thisNorm=pdftmp->getVal(obstmp)*thisObs->getBinWidth(jj);
      if (thisNorm*expectedEvents <= 0)
      {
	cout << "WARNING::Detected bin with zero expected events (" << thisNorm*expectedEvents << ") ! Please check your inputs. Obs = " << thisObs->GetName() << ", bin = " << jj << endl;
      }
      if (thisNorm*expectedEvents > 0 && thisNorm*expectedEvents < pow(10.0, 18)) asimovData->add(*mc->GetObservables(), thisNorm*expectedEvents);
    }
    
    if (_printLevel >= 1)
    {
      asimovData->Print();
      cout <<"sum entries "<<asimovData->sumEntries()<<endl;
    }
    if(asimovData->sumEntries()!=asimovData->sumEntries()){
      cout << "sum entries is nan"<<endl;
      exit(1);
    }

    //((RooRealVar*)obstmp->first())->Print();
    //cout << "expected events " << pdftmp->expectedEvents(*obstmp) << endl;
     
    w->import(*asimovData);

    if (_printLevel >= 1)
    {
      asimovData->Print();
      cout << endl;
    }
  }
  else
  {
    map<string, RooDataSet*> asimovDataMap;

    
    //try fix for sim pdf
    RooCategory* channelCat = (RooCategory*)&simPdf->indexCat();//(RooCategory*)w->cat("master_channel");//(RooCategory*) (&simPdf->indexCat());
    //    TIterator* iter = simPdf->indexCat().typeIterator() ;
    TIterator* iter = channelCat->typeIterator() ;
    RooCatType* tt = NULL;
    int nrIndices = 0;
    while((tt=(RooCatType*) iter->Next())) {
      nrIndices++;
    }
    for (int i=0;i<nrIndices;i++){
      channelCat->setIndex(i);
      iFrame++;
      // Get pdf associated with state from simpdf
      RooAbsPdf* pdftmp = simPdf->getPdf(channelCat->getLabel()) ;
	
      // Generate observables defined by the pdf associated with this state
      RooArgSet* obstmp = pdftmp->getObservables(*mc->GetObservables()) ;

      if (_printLevel >= 1)
      {
	obstmp->Print();
	cout << "on type " << channelCat->getLabel() << " " << iFrame << endl;
      }

      RooDataSet* obsDataUnbinned = new RooDataSet(Form("combAsimovData%d",iFrame),Form("combAsimovData%d",iFrame),RooArgSet(obsAndWeight,*channelCat),WeightVar(*weightVar));
      RooRealVar* thisObs = ((RooRealVar*)obstmp->first());
      double expectedEvents = pdftmp->expectedEvents(*obstmp);
      double thisNorm = 0;
      for(int jj=0; jj<thisObs->numBins(); ++jj){
	thisObs->setBin(jj);

	thisNorm=pdftmp->getVal(obstmp)*thisObs->getBinWidth(jj);
	if (thisNorm*expectedEvents > 0 && thisNorm*expectedEvents < pow(10.0, 18)) obsDataUnbinned->add(*mc->GetObservables(), thisNorm*expectedEvents);
      }
    
      if (_printLevel >= 1)
      {
	obsDataUnbinned->Print();
	cout <<"sum entries "<<obsDataUnbinned->sumEntries()<<endl;
      }
      if(obsDataUnbinned->sumEntries()!=obsDataUnbinned->sumEntries()){
	cout << "sum entries is nan"<<endl;
	exit(1);
      }

      // ((RooRealVar*)obstmp->first())->Print();
      // cout << "pdf: " << pdftmp->GetName() << endl;
      // cout << "expected events " << pdftmp->expectedEvents(*obstmp) << endl;
      // cout << "-----" << endl;

      asimovDataMap[string(channelCat->getLabel())] = obsDataUnbinned;//tempData;

      if (_printLevel >= 1)
      {
	cout << "channel: " << channelCat->getLabel() << ", data: ";
	obsDataUnbinned->Print();
	cout << endl;
      }
    }

    asimovData = new RooDataSet(("asimovData"+muStr.str()).c_str(),("asimovData"+muStr.str()).c_str(),RooArgSet(obsAndWeight,*channelCat),Index(*channelCat),Import(asimovDataMap),WeightVar(*weightVar));
    w->import(*asimovData);
  }

  if(mu_injection > 0) {
    RooRealVar* norm_injection = w->var("ATLAS_norm_muInjection");
    if(norm_injection) {
      norm_injection->setVal(0);
    }
  }

//bring us back to nominal for exporting
  //w->loadSnapshot("nominalNuis");
  w->loadSnapshot("nominalGlobs");

  //ROOT::Math::MinimizerOptions::SetDefaultPrintLevel(printLevel);

  return asimovData;
}
