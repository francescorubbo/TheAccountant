#!/usr/bin/env python

# @file:    CookTheBooks.py
# @purpose: run the analysis
# @author:  Giordon Stark <gstark@cern.ch>
# @date:    April 2015
#
# @example:
# @code
# CookTheBooks.py --help
# @endcode
#

import logging

root_logger = logging.getLogger()
root_logger.addHandler(logging.StreamHandler())
cookBooks_logger = logging.getLogger("cookBooks")

import argparse
import os

# think about using argcomplete
# https://argcomplete.readthedocs.org/en/latest/#activating-global-completion%20argcomplete

if __name__ == "__main__":
  # if we want multiple custom formatters, use inheriting
  class CustomFormatter(argparse.ArgumentDefaultsHelpFormatter):
    pass

  parser = argparse.ArgumentParser(description='Become an accountant and cook the books!',
                                   usage='%(prog)s filename [filename] [options]',
                                   formatter_class=lambda prog: CustomFormatter(prog, max_help_position=30))

  # positional argument, require the first argument to be the input filename
  parser.add_argument('input_filename',
                      metavar='',
                      type=str,
                      nargs='+',
                      help='input file(s) to read')
  parser.add_argument('--submitDir',
                      dest='submit_dir',
                      metavar='<directory>',
                      type=str,
                      required=False,
                      help='Output directory to store the output.',
                      default='submitDir')
  parser.add_argument('--nevents',
                      dest='num_events',
                      metavar='<n>',
                      type=int,
                      help='Number of events to process for all datasets.',
                      default=0)
  parser.add_argument('--skip',
                      dest='skip_events',
                      metavar='<n>',
                      type=int,
                      help='Number of events to skip at start.',
                      default=0)
  parser.add_argument('-f', '--force',
                      dest='force_overwrite',
                      action='store_true',
                      help='Overwrite previous directory if it exists.')

  # http://stackoverflow.com/questions/12303547/set-the-default-to-false-if-another-mutually-exclusive-argument-is-true
  group_driver = parser.add_mutually_exclusive_group()
  group_driver.add_argument('--direct',
                      dest='driver',
                      metavar='',
                      action='store_const',
                      const='direct',
                      help='Run your jobs locally.')
  group_driver.add_argument('--prooflite',
                      dest='driver',
                      metavar='',
                      action='store_const',
                      const='prooflite',
                      help='Run your jobs using ProofLite')
  group_driver.add_argument('--grid',
                      dest='driver',
                      metavar='',
                      action='store_const',
                      const='grid',
                      help='Run your jobs on the grid.')
  group_driver.set_defaults(driver='direct')

  parser.add_argument('--inputList',
                      dest='input_from_file',
                      action='store_true',
                      help='If enabled, will read in a text file containing a list of files.')
  parser.add_argument('--inputDQ2',
                      dest='input_from_DQ2',
                      action='store_true',
                      help='If enabled, will search using DQ2. Can be combined with `--inputList`.')
  parser.add_argument('-v', '--verbose',
                      dest='verbose',
                      action='count',
                      default=0,
                      help='Enable verbose output of various levels. Default: no verbosity')

  group_classifyEvent = parser.add_argument_group('classifyEvent options')
  group_classifyEvent.add_argument('--no-minMassJigsaw',
                                   dest='disable_minMassJigsaw',
                                   action='store_true',
                                   help='Disable the minMass Jigsaw')
  group_classifyEvent.add_argument('--no-contraBoostJigsaw',
                                   dest='disable_contraBoostJigsaw',
                                   action='store_true',
                                   help='Disable the contraBoost Jigsaw')
  group_classifyEvent.add_argument('--no-hemiJigsaw',
                                   dest='hemiJigsaw',
                                   action='store_false',
                                   help='Disable the hemi Jigsaw')
  group_classifyEvent.add_argument('--drawDecayTreePlots',
                                   dest='drawDecayTreePlots',
                                   action='store_true',
                                   help='Enable to draw the decay tree plots and save the canvas in the output ROOT file. Please only enable this if running locally.')
  group_classifyEvent.add_argument('--debug',
                                   dest='debug',
                                   action='store_true',
                                   help='Enable verbose output of the algorithms.')
  group_classifyEvent.add_argument('--eventInfo',
                                   dest='eventInfo',
                                   metavar='',
                                   type=str,
                                   help='EventInfo container name.',
                                   default='EventInfo')
  group_classifyEvent.add_argument('--jets',
                                   dest='inputJets',
                                   metavar='',
                                   type=str,
                                   help='Jet container name.',
                                   default='AntiKt10LCTopoJets')
  group_classifyEvent.add_argument('--bJets',
                                   dest='inputBJets',
                                   metavar='',
                                   type=str,
                                   help='B-Jet container name.',
                                   default='')
  group_classifyEvent.add_argument('--met',
                                   dest='inputMET',
                                   metavar='',
                                   type=str,
                                   help='Missing Et container name.',
                                   default='MET_RefFinal')
  group_classifyEvent.add_argument('--electrons',
                                   dest='inputElectrons',
                                   metavar='',
                                   type=str,
                                   help='Electrons container name.',
                                   default='')
  group_classifyEvent.add_argument('--muons',
                                   dest='inputMuons',
                                   metavar='',
                                   type=str,
                                   help='Muons container name.',
                                   default='')
  group_classifyEvent.add_argument('--taujets',
                                   dest='inputTauJets',
                                   metavar='',
                                   type=str,
                                   help='TauJets container name.',
                                   default='')
  group_classifyEvent.add_argument('--photons',
                                   dest='inputPhotons',
                                   metavar='',
                                   type=str,
                                   help='Photons container name.',
                                   default='')

  # parse the arguments, throw errors if missing any
  args = parser.parse_args()

  # set verbosity for python printing
  if args.verbose < 5:
    cookBooks_logger.setLevel(25 - args.verbose*5)
  else:
    cookBooks_logger.setLevel(logging.NOTSET + 1)

  try:
    import timing
    import ROOT

    cookBooks_logger.info("loading packages")
    ROOT.gROOT.Macro("$ROOTCOREDIR/scripts/load_packages.C")

    if args.force_overwrite:
      cookBooks_logger.info("removing {0:s}".format(args.submit_dir))
      import shutil
      shutil.rmtree(args.submit_dir, True)

    #Set up the job for xAOD access:
    ROOT.xAOD.Init("CookTheBooks").ignore();

    # create a new sample handler to describe the data files we use
    cookBooks_logger.info("creating new sample handler")
    sh_all = ROOT.SH.SampleHandler()

    for fname in args.input_filename:
      if args.input_from_file:
        if args.input_from_DQ2:
          with open(fname, 'r') as f:
            for line in f:
                ROOT.SH.scanDQ2(sh_all, line)
        else:
          ROOT.SH.readFileList(sh_all, "sample", fname)
      else:
        if args.input_from_DQ2:
          ROOT.SH.scanDQ2(sh_all, fname)
        else:
          # need to parse and split it up
          fname_base = os.path.basename(fname)
          sample_dir = os.path.dirname(os.path.abspath(fname))
          mother_dir = os.path.dirname(sample_dir)
          sh_list = ROOT.SH.DiskListLocal(mother_dir)
          ROOT.SH.scanDir(sh_all, sh_list, fname_base, os.path.basename(sample_dir))

    # print out the samples we found
    cookBooks_logger.info("\t%d different datasets found", len(sh_all))
    sh_all.printContent()

    # set the name of the tree in our files
    sh_all.setMetaString("nc_tree", "CollectionTree")

    # this is the basic description of our job
    cookBooks_logger.info("creating new job")
    job = ROOT.EL.Job()
    job.sampleHandler(sh_all)

    if args.num_events > 0:
      cookBooks_logger.info("processing only %d events", args.num_events)
      job.options().setDouble(ROOT.EL.Job.optMaxEvents, args.num_events)

    if args.skip_events > 0:
      cookBooks_logger.info("skipping first %d events", args.skip_events)
      job.options().setDouble(ROOT.EL.Job.optSkipEvents, args.skip_events)

    job.options().setDouble(ROOT.EL.Job.optCacheSize, 50*1024*1024)
    job.options().setDouble(ROOT.EL.Job.optCacheLearnEntries, 50)

    # add our algorithm to the job
    cookBooks_logger.info("creating algorithms")
    classifyEvent = ROOT.ClassifyEvent()
    classifyEvent.m_minMassJigsaw       = not(args.disable_minMassJigsaw)
    classifyEvent.m_contraBoostJigsaw   = not(args.disable_contraBoostJigsaw)
    classifyEvent.m_hemiJigsaw          = not(args.disable_hemiJigsaw)
    classifyEvent.m_drawDecayTreePlots  = args.drawDecayTreePlots
    classifyEvent.m_debug               = args.debug
    classifyEvent.m_eventInfo           = args.eventInfo
    classifyEvent.m_inputJets           = args.inputJets
    classifyEvent.m_inputBJets          = args.inputBJets
    classifyEvent.m_inputMET            = args.inputMET
    classifyEvent.m_inputElectrons      = args.inputElectrons
    classifyEvent.m_inputMuons          = args.inputMuons
    classifyEvent.m_inputTauJets        = args.inputTauJets
    classifyEvent.m_inputPhotons        = args.inputPhotons

    cookBooks_logger.info("adding algorithms")
    job.algsAdd(classifyEvent)


    # make the driver we want to use:
    # this one works by running the algorithm directly
    cookBooks_logger.info("creating driver")
    driver = None
    if (args.driver == "direct"):
      cookBooks_logger.info("\trunning on direct")
      driver = ROOT.EL.DirectDriver()
      cookBooks_logger.info("\tsubmit job")
      driver.submit(job, args.submit_dir)
    elif (args.driver == "prooflite"):
      cookBooks_logger.info("\trunning on prooflite")
      driver = ROOT.EL.ProofDriver()
      cookBooks_logger.info("\tsubmit job")
      driver.submit(job, args.submit_dir)
    elif (args.driver == "grid"):
      cookBooks_logger.info("\trunning on Grid")
      driver = ROOT.EL.PrunDriver()
      driver.options().setString("nc_outputSampleName", "user.gstark.%%in:name[2]%%.%%in:name[3]%%")
      #driver.options().setDouble("nc_disableAutoRetry", 1)
      driver.options().setDouble("nc_nFilesPerJob", 1)
      driver.options().setDouble(ROOT.EL.Job.optGridMergeOutput, 1);
      cookBooks_logger.info("\tsubmit job")
      driver.submitOnly(job, args.submit_dir)

  except Exception, e:
    # we crashed
    cookBooks_logger.exception("{0}\nAn exception was caught!".format("-"*20))