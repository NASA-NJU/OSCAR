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
#ifndef JSON_OUTPUTS_H
#define JSON_OUTPUTS_H

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dc-topology.h"
#include "ns3/dcb-traffic-gen-application-helper.h"
#include "ns3/dcb-traffic-gen-application.h"
#include "ns3/error-model.h"
#include "ns3/flow-identifier.h"
#include "ns3/global-route-manager.h"
#include "ns3/global-value.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/real-time-application.h"
#include "ns3/traffic-control-module.h"

#include <boost/json.hpp>
#include <set>

namespace ns3
{

namespace json_util
{

/**
 * \brief Write statistics to the file specified in the config.
 *
 * This function will also write the config to the file to keep the mapping from config to stats.
 *
 * \param conf The CDF config in a json object.
 * \param apps The trace application container.
 * \param topology The topology.
 * \param confFileName The file name of the config file.
 */
void OutputStats(boost::json::object& conf,
                 ApplicationContainer& apps,
                 Ptr<DcTopology> topology,
                 std::string confFileName);

/**
 * \brief Create the output file and return the file stream.
 */
std::ofstream CreateOutputFile(boost::json::object& conf, std::string confFileName);

/**
 * \brief Store the app stats into a json object and return it.
 *
 * \return A json object containing the app stats, use shared_ptr as it is a heavy object.
 */
std::shared_ptr<boost::json::object> ConstructAppStatsObj(ApplicationContainer& apps);

typedef std::map<FlowIdentifier, std::shared_ptr<boost::json::object>> FlowStatsObjMap;

/**
 * \brief Construct flow stats from sender side.
 * \param apps The trace application container.
 * \param mFlowStatsObjs The map to store the flow stats.
 */
void ConstructSenderFlowStats(ApplicationContainer& apps, FlowStatsObjMap& mFlowStatsObjs);

/**
 * \brief Construct flow stats from receiver side.
 * \param apps The trace application container.
 * \param mFlowStatsObjs The map to store the flow stats.
 */
void ConstructRealTimeFlowStats(ApplicationContainer& apps, FlowStatsObjMap& mFlowStatsObjs);

/**
 * \brief Construct switch stats.
 * \param topology The topology, used to get the switch.
 * \param switchStatsObjs The map to store the switch stats.
 * \param startTime The start time of the simulation, used to calc avg qlength.
 * \param finishTime The finish time of the last flow, used to calc avg qlength.
 */
void ConstructSwitchStats(Ptr<DcTopology> topology,
                          boost::json::object& switchStatsObjs,
                          Time startTime,
                          Time finishTime);

/**
 * \brief Construct switch stats.
 * \param topology The topology, used to get the switch.
 * \param switchStatsObjs The map to store the switch stats.
 * \param startTime The start time of the simulation, used to calc avg qlength.
 * \param finishTime The finish time of the last flow, used to calc avg qlength.
 */
void ConstructPPFCSwitchStats(Ptr<DcTopology> topology,
                              boost::json::object& switchStatsObjs,
                              Time startTime,
                              Time finishTime);

/**
 * \brief Disable the detailed switch stats for some switches.
 *
 * Used to save time and space when the topology is large.
 */
void DisableDetailedSwitchStats(boost::json::object& configObj, Ptr<DcTopology> topology);

} // namespace json_util

} // namespace ns3

#endif /* JSON_OUTPUTS_H */
