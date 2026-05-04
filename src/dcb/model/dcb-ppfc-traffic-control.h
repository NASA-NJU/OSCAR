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
 * Author: ChangLiu <liuchang_1307@163.com>
 */

#ifndef DCB_PPFC_TRAFFIC_CONTROL_H
#define DCB_PPFC_TRAFFIC_CONTROL_H

#include "dcb-flow-control-port.h"
#include "dcb-net-device.h"
#include "dcb-traffic-control.h"

#include "ns3/dc-topology.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/net-device.h"
#include "ns3/pfc-frame.h"
#include "ns3/tag-buffer.h"
#include "ns3/traffic-control-layer.h"

#include <vector>

namespace ns3
{

class Packet;
class QueueDisc;
class NetDeviceQueueInterface;
class DcbFlowControlPort;

/**
 * \defgroup dcb
 *
 * Inherit from Traffic Control layer aims at introducing an equivalent of the Linux Traffic
 * Control infrastructure into ns-3. The Traffic Control layer sits in between
 * the NetDevices (L2) and any network protocol (e.g., IP). It is in charge of
 * processing packets and performing actions on them: scheduling, dropping,
 * marking, policing, etc.
 *
 * per-port-traffic-control: Compared with the parent class, this module adds logic for selecting
 queues in
 * egress port, that is selected according to the topology and routing algorithm.
 *
 * \ingroup traffic-control
 *
 * \brief Traffic control layer class
 *
 * This object represents the main interface of the Traffic Control Module.
 * Basically, we manage both IN and OUT directions (sometimes called RX and TX,
 * respectively). The OUT direction is easy to follow, since it involves
 * direct calls: upper layer (e.g. IP) calls the Send method on an instance of
 * this class, which then calls the Enqueue method of the QueueDisc associated
 * with the device. The Dequeue method of the QueueDisc finally calls the Send
 * method of the NetDevice.
 *
 * The IN direction uses a little trick to reduce dependencies between modules.
 * In simple words, we use Callbacks to connect upper layer (which should register
 * their Receive callback through RegisterProtocolHandler) and NetDevices.
 *
 * An example of the IN connection between this layer and IP layer is the following:
 *\verbatim
  Ptr<TrafficControlLayer> tc = m_node->GetObject<TrafficControlLayer> ();

  NS_ASSERT (tc != 0);

  m_node->RegisterProtocolHandler (MakeCallback (&TrafficControlLayer::Receive, tc),
                                   Ipv4L3Protocol::PROT_NUMBER, device);
  m_node->RegisterProtocolHandler (MakeCallback (&TrafficControlLayer::Receive, tc),
                                   ArpL3Protocol::PROT_NUMBER, device);

  tc->RegisterProtocolHandler (MakeCallback (&Ipv4L3Protocol::Receive, this),
                               Ipv4L3Protocol::PROT_NUMBER, device);
  tc->RegisterProtocolHandler (MakeCallback (&ArpL3Protocol::Receive, PeekPointer
 (GetObject<ArpL3Protocol> ())), ArpL3Protocol::PROT_NUMBER, device); \endverbatim
 * On the node, for IPv4 and ARP packet, is registered the
 * TrafficControlLayer::Receive callback. At the same time, on the TrafficControlLayer
 * object, is registered the callbacks associated to the upper layers (IPv4 or ARP).
 *
 * When the node receives an IPv4 or ARP packet, it calls the Receive method
 * on TrafficControlLayer, that calls the right upper-layer callback once it
 * finishes the operations on the packet received.
 *
 * Discrimination through callbacks (in other words: what is the right upper-layer
 * callback for this packet?) is done through checks over the device and the
 * protocol number.
 */
class DcbPPfcTrafficControl : public DcbTrafficControl
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Get the type ID for the instance
     * \return the instance TypeId
     */
    virtual TypeId GetInstanceTypeId(void) const override;

    /**
     * \brief Constructor
     */
    DcbPPfcTrafficControl();

    virtual ~DcbPPfcTrafficControl();

    // Delete copy constructor and assignment operator to avoid misuse
    DcbPPfcTrafficControl(const DcbPPfcTrafficControl&) = delete;
    DcbPPfcTrafficControl& operator=(const DcbPPfcTrafficControl&) = delete;

    /**
     * Register NetDevice number for PFC to initiate counters.
     */
    virtual void RegisterDeviceNumber(const uint32_t num);

    /**
     * \brief Called from upper layer to queue a packet for the transmission.
     *
     * \param device the device the packet must be sent to
     * \param item a queue item including a packet and additional information
     */
    virtual void Send(Ptr<NetDevice> device, Ptr<QueueDiscItem> item) override;

    /**
     * \brief Called after egress queue pops out a packet.
     * For example, it can be used for flow control doing some egress action.
     */
    void EgressProcess(uint32_t port, uint32_t qIdx, Ptr<Packet> packet);

    void InstallFCToPort(uint32_t portIdx,
                         Ptr<DcbFlowControlPort> fc,
                         std::vector<ns3::Ptr<ns3::DcbFlowControlMmuQueue>> fcMmuQueues);

    /**
     * \brief when enqueue a packet in egress do check if this egress port is congested
     * and send pause to upstreams (only check on switch).
     * \param port the egress port that enqueue a packet
     * \param qIdx the index of queue that the paceket in
     */
    void EnqueueEgressProcess(uint32_t port, uint32_t qIdx);

    /**
     * \brief when dequeue a packet in egress do check if this egress port is uncongested
     * and send resume to upstreams (only check on switch).
     * \param port the egress port that dequeue a packet
     * \param qIdx the index of queue that the paceket in
     */
    void DequeueEgressProcess(uint32_t port, uint32_t qIdx);

    /**
     * \brief get the category of qIdx in port.
     * \param portIdx the index of port
     * \param qIdx the index of queue
     * \return the category of the queue, used in flow control.
     */
    uint32_t GetCategory(uint32_t portIdx, uint32_t qIdx);

    /**
     * \brief check if port is congested/uncongested.
     * \param portIdx the index of port
     * \param cate the category we check
     * \param th the on or off threshold of this category
     * \return if qLen > th, return 1; qLen == th, return 0; qLen < th, return -1.
     */

    int CompareEgressQueueLength(uint32_t portIdx, uint32_t cate, uint32_t th);

    /**
     * \brief set the global topology info to traffic control layer.
     * \param topo the global topology
     */
    void SetTopology(Ptr<DcTopology> topo);

    Ptr<DcTopology> GetTopology() const;

    /**
     * \brief get the priority in egressport.
     * \param devIdx the index of egressport
     * \param srcIp the source ip of packet
     * \param dstIp the dest ip of packet
     * \param prio the original priority of packet in socket layer.
     */
    uint32_t GetEgressPriority(const uint32_t devIdx,
                               const Ipv4Address& srcIp,
                               const Ipv4Address& dstIp,
                               const uint32_t srcPort,
                               const uint32_t dstPort,
                               const uint32_t prio);

    /**
     * \brief relay a received pfc to upstream. only be called in host!
     * \param devIdx the index of device to send the pfc to upstream switch.
     * \param cate the category of congested Q in downstream.
     * \param pauseQIdx the congested port number - 1 in downstream.
     * \param from not used.
     * \param type the type of pfc packet. 0 for pause, 1 for resume.
     * \
     */
    void RelayPFC(uint32_t devIdx,
                  uint32_t cate,
                  uint32_t pauseQIdx,
                  const Address& from,
                  uint32_t type);

    // priority number. (diff in host and switch)

    class PortInfo
    {
      public:
        PortInfo();

        inline void SetFC(Ptr<DcbFlowControlPort> fc,
                          std::vector<Ptr<DcbFlowControlMmuQueue>> fcMmuQueues)
        {
            m_fcEnabled = true;
            m_fc = fc;
            m_fcMmuQueues = fcMmuQueues;
        }

        inline bool FcEnabled() const
        {
            return m_fcEnabled;
        }

        inline Ptr<DcbFlowControlPort> GetFC() const
        {
            return m_fc;
        }

        inline Ptr<DcbFlowControlMmuQueue> GetFCMmuQueue(uint32_t priority) const
        {
            if (priority > m_fcMmuQueues.size())
            {
                NS_FATAL_ERROR("Priority is out of range");
            }
            return m_fcMmuQueues[priority];
        }

        inline uint32_t GetFCMmuQueueSize() const
        {
            return m_fcMmuQueues.size();
        }

      private:
        bool m_fcEnabled;
        Ptr<DcbFlowControlPort> m_fc;
        std::vector<Ptr<DcbFlowControlMmuQueue>> m_fcMmuQueues;
    }; // class PortInfo

    // Used to bind traces
    inline std::vector<PortInfo>& GetPorts()
    {
        return m_buffer.GetPorts();
    }

  private:
    class Buffer
    {
      public:
        Buffer();
        void SetBufferSpace(uint32_t bytes);
        void RegisterPortNumber(const uint32_t num);
        /**
         * \brief Process when packet received.
         * Returns whether the packet is accomondated into the buffer, false for packet drop.
         */
        // the outQueuePriority is changed by per-port-fc, not only 8 queues!
        bool InPacketProcess(uint32_t inPortIndex,
                             uint32_t inQueuePriority,
                             uint32_t outPortIndex,
                             uint32_t outQueuePriority,
                             uint32_t packetSize);
        /**
         * \brief check if packet can be put into egress port. used when host send a packet.
         */
        bool InPacketProcess(uint32_t outPortIndex, uint32_t outQueuePriority, uint32_t packetSize);
        void OutPacketProcess(uint32_t inPortIndex,
                              uint32_t inQueuePriority,
                              uint32_t outPortIndex,
                              uint32_t outQueuePriority,
                              uint32_t packetSize);
        void OutPacketProcess(uint32_t outPortIndex,
                              uint32_t outQueuePriority,
                              uint32_t packetSize);

        inline PortInfo& GetPort(uint32_t portIndex)
        {
            return m_ports[portIndex];
        }

        inline std::vector<PortInfo>& GetPorts()
        {
            return m_ports;
        }

        inline uint32_t GetSize() const
        {
            return m_totalSize;
        }

        uint32_t GetSharedSize();
        uint32_t GetSharedUsed();

        uint32_t GetBufferUsed();

        void SetPortFcMmuBufferCallback(uint32_t portIndex);

      private:
        uint32_t m_totalSize;
        std::vector<PortInfo> m_ports;
    }; // class Buffer

    Buffer m_buffer;
    Ptr<DcTopology> m_topology;
};

} // namespace ns3

#endif // DCB_PPFC_TRAFFIC_CONTROL_H
