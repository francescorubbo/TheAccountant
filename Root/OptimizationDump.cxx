#include <EventLoop/Job.h>
#include <EventLoop/StatusCode.h>
#include <EventLoop/Worker.h>
#include "EventLoop/OutputStream.h"
#include <TheAccountant/OptimizationDump.h>

// EDM includes
#include "xAODEventInfo/EventInfo.h"
#include "xAODJet/JetContainer.h"
#include "xAODMuon/MuonContainer.h"
#include "xAODEgamma/ElectronContainer.h"
#include "xAODEgamma/PhotonContainer.h"
#include "xAODTau/TauJetContainer.h"
#include "xAODTruth/TruthParticleContainer.h"
#include "xAODMissingET/MissingETContainer.h"
//#include "xAODBTaggingEfficiency/BTaggingEfficiencyTool.h"
#include "xAODBTagging/BTagging.h"

// Infrastructure includes
#include "xAODRootAccess/Init.h"

// xAH includes
#include "xAODAnaHelpers/HelperFunctions.h"
#include "xAODAnaHelpers/tools/ReturnCheck.h"

#include "TheAccountant/VariableDefinitions.h"

// root includes
#include <TFile.h>

// c++ includes
#include <set>
#include <stdlib.h>

namespace HF = HelperFunctions;
namespace VD = VariableDefinitions;

// this is needed to distribute the algorithm to the workers
ClassImp(OptimizationDump)
OptimizationDump :: OptimizationDump () :
  m_tree(new TTree("oTree", "optimization tree")),
  m_eventWeight(0.0),
  m_effectiveMass(-999.0),
  m_totalTransverseMomentum(-999.0),
  m_totalTransverseMass(-999.0),
  m_numJets(-99),
  m_numJetsLargeR(-99),
  m_n_topTag_Loose(0),
  m_n_topTag_Medium(0),
  m_n_topTag_Tight(0),
  m_jetReclusteringTools(3, nullptr),
  m_rc_pt(4, -99),
  m_rc_m(4, -99),
  m_rc_split12(4, -99),
  m_rc_split23(4, -99),
  m_rc_nsj(4, -99)
{}

EL::StatusCode OptimizationDump :: setupJob (EL::Job& job)
{
  job.useXAOD();
  xAOD::Init("OptimizationDump").ignore();

  /* https://twiki.cern.ch/twiki/bin/viewauth/AtlasProtected/EventLoop#Creating_output_n_tuples */
  // output file for optimization tree
  EL::OutputStream out ("optimizationTree");
  job.outputAdd (out);

  return EL::StatusCode::SUCCESS;
}

EL::StatusCode OptimizationDump :: histInitialize () { return EL::StatusCode::SUCCESS; }
EL::StatusCode OptimizationDump :: fileExecute () { return EL::StatusCode::SUCCESS; }
EL::StatusCode OptimizationDump :: changeInput (bool /*firstFile*/) { return EL::StatusCode::SUCCESS; }

EL::StatusCode OptimizationDump :: initialize () {
  // assign m_event and m_store
  m_event = wk()->xaodEvent();
  m_store = wk()->xaodStore();

  TFile *file = wk()->getOutputFile ("optimizationTree");
  m_tree->SetDirectory (file);

  m_tree->Branch ("event.weight",    &m_eventWeight, "event.weight/F");
  m_tree->Branch ("m.effective", &m_effectiveMass, "m.effective/F");
  m_tree->Branch ("pt.total",   &m_totalTransverseMomentum, "pt.total/F");
  m_tree->Branch ("m.transverse",   &m_totalTransverseMass, "m.transverse/F");
  m_tree->Branch ("multiplicity.jet",  &m_numJets, "multiplicity.jet/I");
  m_tree->Branch ("multiplicity.jet.largeR",  &m_numJetsLargeR, "multiplicity.jet.largeR/I");

  m_tree->Branch ("multiplicity.topTag.loose", &m_n_topTag_Loose, "multiplicity.topTag.loose/I");
  m_tree->Branch ("multiplicity.topTag.medium", &m_n_topTag_Medium, "multiplicity.topTag.medium/I");
  m_tree->Branch ("multiplicity.topTag.tight", &m_n_topTag_Tight, "multiplicity.topTag.tight/I");

  // initialize branches for reclustered jets
  for(int i=0; i<4; i++){
    for(int r=8; r<14; r+=2){
      std::string commonDenominator = "jet.rc"+std::to_string(r)+"."+std::to_string(i);
      std::string branchName;

      branchName = "pt."+commonDenominator;
      m_tree->Branch(branchName.c_str(), &(m_rc_pt[i]), (branchName+"/F").c_str());

      branchName = "m."+commonDenominator;
      m_tree->Branch(branchName.c_str(), &(m_rc_m[i]), (branchName+"/F").c_str());

      branchName = "split12."+commonDenominator;
      m_tree->Branch(branchName.c_str(), &(m_rc_split12[i]), (branchName+"/F").c_str());

      branchName = "split23."+commonDenominator;
      m_tree->Branch(branchName.c_str(), &(m_rc_split23[i]), (branchName+"/F").c_str());

      branchName = "nsj."+commonDenominator;
      m_tree->Branch(branchName.c_str(), &(m_rc_nsj[i]), (branchName+"/I").c_str());
    }
  }

  for(int i=0; i<3; i++){
    char outputContainer[8];
    float radius = 0.8 + (0.2*i); // 0.8, 1.0, 1.2
    sprintf(outputContainer, "RC%02.0fJets", radius*10);
    m_jetReclusteringTools[i] = new JetReclusteringTool(outputContainer+std::to_string(std::rand()));
    RETURN_CHECK("initialize()", m_jetReclusteringTools[i]->setProperty("InputJetContainer",  m_inputJets), "");
    RETURN_CHECK("initialize()", m_jetReclusteringTools[i]->setProperty("OutputJetContainer", outputContainer), "");
    RETURN_CHECK("initialize()", m_jetReclusteringTools[i]->setProperty("ReclusterRadius",    radius), "");
    RETURN_CHECK("initialize()", m_jetReclusteringTools[i]->initialize(), "");
  }

  return EL::StatusCode::SUCCESS;
}

EL::StatusCode OptimizationDump :: execute ()
{
  const xAOD::EventInfo*                eventInfo     (nullptr);
  const xAOD::JetContainer*             in_jetsLargeR (nullptr);
  const xAOD::JetContainer*             in_jets       (nullptr);
  const xAOD::MissingETContainer*       in_missinget  (nullptr);
  const xAOD::ElectronContainer*        in_electrons  (nullptr);
  const xAOD::MuonContainer*            in_muons      (nullptr);
  const xAOD::TauJetContainer*          in_taus       (nullptr);
  const xAOD::PhotonContainer*          in_photons    (nullptr);

  // start grabbing all the containers that we can
  RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(eventInfo,    m_eventInfo,        m_event, m_store, m_debug), "Could not get the EventInfo container.");

  static SG::AuxElement::Accessor< int > pass_preSel("pass_preSel");
  static SG::AuxElement::Accessor< float > decor_eventWeight("eventWeight");
  if(pass_preSel.isAvailable(*eventInfo) && pass_preSel(*eventInfo) == 0) return EL::StatusCode::SUCCESS;

  RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_jetsLargeR,      m_inputLargeRJets,        m_event, m_store, m_debug), "Could not get the inputLargeRJets container.");
  RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_jets,     m_inputJets,       m_event, m_store, m_debug), "Could not get the inputJets container.");

  if(!m_inputMET.empty())
    RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_missinget, m_inputMET,         m_event, m_store, m_debug), "Could not get the inputMET container.");
  if(!m_inputElectrons.empty())
    RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_electrons, m_inputElectrons,   m_event, m_store, m_debug), "Could not get the inputElectrons container.");
  if(!m_inputMuons.empty())
    RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_muons,     m_inputMuons,       m_event, m_store, m_debug), "Could not get the inputMuons container.");
  if(!m_inputTauJets.empty())
    RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_taus,      m_inputTauJets,     m_event, m_store, m_debug), "Could not get the inputTauJets container.");
  if(!m_inputPhotons.empty())
    RETURN_CHECK("OptimizationDump::execute()", HF::retrieve(in_photons,   m_inputPhotons,     m_event, m_store, m_debug), "Could not get the inputPhotons container.");

  // retrieve CalibMET_RefFinal for METContainer
  xAOD::MissingETContainer::const_iterator met_final = in_missinget->find("Final");
  if (met_final == in_missinget->end()) {
    Error("execute()", "No RefFinal inside MET container" );
    return EL::StatusCode::FAILURE;
  }
  // dereference the iterator since it's just a single object
  const xAOD::MissingET* in_met = *met_final;

  // build the reclustered, trimmed jets
  for(auto tool: m_jetReclusteringTools)
    tool->execute();

  // compute variables for optimization
  m_eventWeight = 0.0;
  if(decor_eventWeight.isAvailable(*eventInfo)) m_eventWeight = decor_eventWeight(*eventInfo);

  // compute optimization variables
  m_effectiveMass = VD::Meff(in_met, in_jets, in_jets->size(), in_muons, in_electrons);
  m_totalTransverseMomentum = VD::HT(in_jets, in_muons, in_electrons);
  m_totalTransverseMass = VD::mT(in_met, in_muons, in_electrons);
  m_numJets = in_jets->size();
  m_numJetsLargeR = in_jetsLargeR->size();

  // tagging variables
  m_n_topTag_Loose  = VD::topTag(eventInfo, in_jetsLargeR, VD::WP::Loose);
  m_n_topTag_Medium = VD::topTag(eventInfo, in_jetsLargeR, VD::WP::Medium);
  m_n_topTag_Tight  = VD::topTag(eventInfo, in_jetsLargeR, VD::WP::Tight);

  // fill in all variables
  m_tree->Fill();

  return EL::StatusCode::SUCCESS;
}

EL::StatusCode OptimizationDump :: postExecute () { return EL::StatusCode::SUCCESS; }

EL::StatusCode OptimizationDump :: finalize () {
  for(auto tool: m_jetReclusteringTools)
    if(tool) delete tool;
  return EL::StatusCode::SUCCESS;
}

EL::StatusCode OptimizationDump :: histFinalize () { return EL::StatusCode::SUCCESS; }