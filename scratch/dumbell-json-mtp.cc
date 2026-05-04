/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2023
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Zhaochen Zhang (zhaochen.zhang@outlook.com)
 */

#include "ns3/core-module.h"
#include "ns3/nstime.h"
#include "ns3/log.h"
#include "ns3/dcb-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/packet.h"
#include "ns3/error-model.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mtp-module.h"
#include "ns3/network-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/global-value.h"
#include "ns3/json-util-module.h"
#include "ns3/json-topology-helper.h"

#include <boost/json.hpp>
#include <iostream>
#include <fstream>
#include <time.h>
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ScratchSimulator");

int main (int argc, char *argv[])
{
  NS_LOG_UNCOND ("Scratch Simulator");

  MtpInterface::Enable();
  // Statistics about simulation running time
  clock_t begint, endt;
  begint = clock ();

  std::string config_file;
  if (argc > 1)
    {
      //Read the configuration file
      config_file = std::string (argv[1]);
    }
  else
    {
      std::cout << "Error: require a configuration file\n";
      fflush (stdout);
      return 1;
    }

  LogComponentEnableAll (LOG_LEVEL_WARN);
  // LogComponentEnable ("PausableQueueDisc", LOG_LEVEL_INFO);
  // LogComponentEnable ("FifoQueueDiscEcn", LOG_LEVEL_INFO);

  // Read config in json from config file
  boost::json::object configObj = json_util::ReadConfig (config_file);

  // Automatically set default values using ns3 Config system
  json_util::SetDefault (configObj["defaultConfig"].get_object ());

  // Set global seed for random generator
  json_util::SetRandomSeed (configObj["runtimeConfig"].get_object ()
                            .find ("seed")->value ().as_int64 ());

  // Build topology from topology file
  Ptr<DcTopology> topology = json_util::BuildTopology (configObj);

  // Install applications
  json_util::InstallApplications (configObj, topology);

  tracer_extension::ConfigOutputDirectory ("data");
  tracer_extension::ConfigTraceFCT (tracer_extension::Protocol::RoCEv2, "fct.csv");
  tracer_extension::ConfigStopTime (MilliSeconds(6));

  tracer_extension::EnableBufferoverflowTrace (topology->GetNode (4).nodePtr, "sw4");
  tracer_extension::EnableBufferoverflowTrace (topology->GetNode (5).nodePtr, "sw5");

  Ptr<NetDevice> swDev = topology->GetNetDeviceOfNode(4, 2);
  tracer_extension::EnableDeviceRateTrace(swDev, "sw4", MicroSeconds (100));

  tracer_extension::EnablePortQueueLengthTrace(topology->GetNetDeviceOfNode(4, 1), "sw4-0", MicroSeconds (10));
  tracer_extension::EnablePortQueueLengthTrace(topology->GetNetDeviceOfNode(5, 2), "sw5-2", MicroSeconds (10));

  Simulator::Run ();
  endt = clock ();
  std::cout << "Total used time: " << (double) (endt - begint) / CLOCKS_PER_SEC << "s" << std::endl;
  Simulator::Destroy ();
}
