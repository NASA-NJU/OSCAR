/*
 * Copyright (c) 2008 INRIA
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
 * Author: Pavinberg <pavin0702@gmail.com>
 */

#ifndef DCB_FC_HELPER_H
#define DCB_FC_HELPER_H

#include "ns3/dcb-hpcc-port.h"
#include "ns3/dcb-pfc-mmu-queue.h"
#include "ns3/dcb-pfc-port.h"
#include "ns3/dcb-ppfc-mmu-queue.h"
#include "ns3/dcb-ppfc-port.h"
#include "ns3/dcb-ppfc-traffic-control.h"
#include "ns3/ppfc-frame.h"
#include "ns3/ppfc-pausable-queue-disc.h"
#include "ns3/object.h"
#include "ns3/object-factory.h"

namespace ns3
{

class DcbFcHelper
{
  public:
    DcbFcHelper();

    virtual ~DcbFcHelper();

    static void InstallPFCtoNodePort(Ptr<Node> node,
                                     const uint32_t port,
                                     const DcbPfcPortConfig& config);

    /*
     * Install PFC to host port
     */
    static void InstallPFCtoHostPort(Ptr<Node> node, const uint32_t port, const uint8_t enableVec);

    // ... other flow control configurtions should be added here

    static void InstallHpccPFCtoNodePort(Ptr<Node> node,
                                         const uint32_t port,
                                         const DcbPfcPortConfig& config);
    static void InstallPPfctoSwitch(Ptr<Node> node,
                                    const uint32_t port,
                                    const DcbPPfcPortConfig& config,
                                    const uint32_t qNum);
    static void InstallPPfctoHost(Ptr<Node> node, const uint32_t port, const uint32_t qNum);

    /**
     * \brief Install all fc-related objects to the node
     */
    void Install(Ptr<Node> node);

    void SetTrafficControlTypeId(const std::string& typeId);
    void SetFlowControlPortTypeId(const std::string& typeId);
    void SetFlowControlMmuQueueTypeId(const std::string& typeId);
    void SetOuterQueueDiscTypeId(const std::string& typeId);
    void SetInnerQueueDiscTypeId(const std::string& typeId);

    typedef std::pair<std::string, Ptr<AttributeValue>> ConfigEntry_t;
    void SetTrafficControlAttributes(std::vector<ConfigEntry_t>&& tcAttributes);
    void SetFlowControlPortAttributes(std::vector<ConfigEntry_t>&& fcpAttributes);
    void SetFlowControlMmuQueueAttributes(std::vector<ConfigEntry_t>&& fcmqAttributes);
    void SetOuterQueueDiscAttributes(std::vector<ConfigEntry_t>&& oqdAttributes);
    void SetInnerQueueDiscAttributes(std::vector<ConfigEntry_t>&& iqdAttributes);
    
    void SetBufferSize(QueueSize bufferSize);
    void SetBufferPerPort(QueueSize bufferPerPort);
    void SetNumQueuePerPort(uint32_t numQueuePerPort);
    void SetBufferBandwidthRatio(double bufferBandwidthRatio);
    void SetNumLosslessQueue(uint32_t numLosslessQueue);

    uint32_t GetNumQueuePerPort() const;
    void SetPriorities(std::vector<uint32_t>&& priorities);
    void SetQuantum(std::vector<uint32_t>&& quantum);
    void SetMaxCredit(uint32_t maxCredit);

  private:
    ObjectFactory m_tcFactory;
    ObjectFactory m_fcpFactory;
    ObjectFactory m_fcmqFactory;
    ObjectFactory m_oqdFactory;
    ObjectFactory m_iqdFactory;

    QueueSize m_bufferSize; // The buffer size of the whole node
    QueueSize m_bufferPerPort;
    double m_bufferBandwidthRatio; // The ratio is in MB / Tbps
    uint32_t m_numQueuePerPort;
    uint32_t m_numLosslessQueue;

    std::vector<uint32_t> m_priorities;
    std::vector<uint32_t> m_quantum; // in KB
    uint32_t m_maxCredit; // in KB
}; // class DcbFcHelper

} // namespace ns3

#endif
