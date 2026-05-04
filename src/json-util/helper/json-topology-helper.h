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
#ifndef JSON_TOPOLGY_HELPER_H
#define JSON_TOPOLGY_HELPER_H

#include "ns3/application-container.h"
#include "ns3/boolean.h"
#include "ns3/core-module.h"
#include "ns3/data-rate.h"
#include "ns3/dc-topology.h"
#include "ns3/dcb-channel.h"
#include "ns3/dcb-fc-helper.h"
#include "ns3/dcb-net-device.h"
#include "ns3/dcb-pfc-port.h"
#include "ns3/dcb-ppfc-port.h"
#include "ns3/dcb-stack-helper.h"
#include "ns3/dcb-traffic-gen-application-helper.h"
#include "ns3/global-router-interface.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-global-routing.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/net-device-container.h"
#include "ns3/net-device.h"
#include "ns3/nstime.h"
#include "ns3/object-factory.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-size.h"
#include "ns3/socket.h"
#include "ns3/switch-node.h"
#include "ns3/traced-value.h"

#include <boost/json.hpp>

namespace ns3
{

namespace json_util
{

Ptr<DcTopology> BuildTopology(boost::json::object& configObj);

}

} // namespace ns3

#endif /* JSON_TOPOLGY_HELPER_H */
