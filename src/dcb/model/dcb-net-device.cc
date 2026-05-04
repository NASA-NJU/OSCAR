/*
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

#include "dcb-net-device.h"

#include "dcb-channel.h"

#include "ns3/boolean.h"
#include "ns3/error-model.h"
#include "ns3/ethernet-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/log-macros-enabled.h"
#include "ns3/pointer.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/type-id.h"
#include "ns3/ppfc-pausable-queue-disc.h"

#include <stdio.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbNetDevice");

NS_OBJECT_ENSURE_REGISTERED(DcbNetDevice);

TypeId
DcbNetDevice::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::DcbNetDevice")
            .SetParent<NetDevice>()
            .SetGroupName("Dcb")
            .AddConstructor<DcbNetDevice>()
            .AddAttribute("Mtu",
                          "The MAC-level Maximum Transmission Unit",
                          UintegerValue(DEFAULT_MTU),
                          MakeUintegerAccessor(&DcbNetDevice::SetMtu, &DcbNetDevice::GetMtu),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("Address",
                          "The MAC address of this device.",
                          Mac48AddressValue(Mac48Address("ff:ff:ff:ff:ff:ff")),
                          MakeMac48AddressAccessor(&DcbNetDevice::m_address),
                          MakeMac48AddressChecker())
            .AddAttribute("DataRate",
                          "The default data rate for point to point links",
                          DataRateValue(DataRate("100Gb/s")),
                          MakeDataRateAccessor(&DcbNetDevice::m_bps),
                          MakeDataRateChecker())
            .AddAttribute("FcEnabled",
                          "Enable flow control functions",
                          BooleanValue(false),
                          MakeBooleanAccessor(&DcbNetDevice::m_fcEnabled),
                          MakeBooleanChecker())
            .AddAttribute("ReceiveErrorModel",
                          "The receiver error model used to simulate packet loss",
                          PointerValue(),
                          MakePointerAccessor(&DcbNetDevice::m_receiveErrorModel),
                          MakePointerChecker<ErrorModel>())
            .AddAttribute("InterframeGap",
                          "The time to wait between packet (frame) transmissions",
                          TimeValue(Seconds(0.0)),
                          MakeTimeAccessor(&DcbNetDevice::m_tInterframeGap),
                          MakeTimeChecker())
            //
            // Transmit queueing discipline for the device which includes its own set
            // of trace hooks.
            //
            .AddAttribute("TxQueue",
                          "A queue to use as the transmit queue in the device.",
                          PointerValue(),
                          MakePointerAccessor(&DcbNetDevice::m_queue),
                          MakePointerChecker<Queue<Packet>>())
            //
            // Trace sources at the "top" of the net device, where packets transition
            // to/from higher layers.
            //
            .AddTraceSource("MacTx",
                            "Trace source indicating a packet has arrived "
                            "for transmission by this device",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_macTxTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("MacTxDrop",
                            "Trace source indicating a packet has been dropped "
                            "by the device before transmission",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_macTxDropTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("MacRx",
                            "A packet has been received by this device, "
                            "has been passed up from the physical layer "
                            "and is being forwarded up the local protocol stack.  "
                            "This is a non-promiscuous trace,",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_macRxTrace),
                            "ns3::Packet::TracedCallback")
            //
            // Trace sources at the "bottom" of the net device, where packets transition
            // to/from the channel.
            //
            .AddTraceSource("PhyTxBegin",
                            "Trace source indicating a packet has begun "
                            "transmitting over the channel",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxBeginTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyTxBeginWithId",
                            "Trace source indicating a packet has begun "
                            "transmitting over the channel",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxBeginWithIdTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyTxEnd",
                            "Trace source indicating a packet has been "
                            "completely transmitted over the channel",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxEndTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyTxDrop",
                            "Trace source indicating a packet has been "
                            "dropped by the device during transmission",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxDropTrace),
                            "ns3::Packet::TracedCallback")
#if 0
          // Not currently implemented for this device
          .AddTraceSource ("PhyRxBegin",
                           "Trace source indicating a packet has begun "
                           "being received by the device",
                           MakeTraceSourceAccessor (&DcbNetDevice::m_phyRxBeginTrace),
                           "ns3::Packet::TracedCallback")
#endif
            .AddTraceSource("PhyRxEnd",
                            "Trace source indicating a packet has been "
                            "completely received by the device",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_phyRxEndTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyRxDrop",
                            "Trace source indicating a packet has been "
                            "dropped by the device during reception",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_phyRxDropTrace),
                            "ns3::Packet::TracedCallback")
            //
            // Trace sources designed to simulate a packet sniffer facility (tcpdump).
            // Note that there is really no difference between promiscuous and
            // non-promiscuous traces in a point-to-point link.
            //
            .AddTraceSource("Sniffer",
                            "Trace source simulating a non-promiscuous packet sniffer "
                            "attached to the device",
                            MakeTraceSourceAccessor(&DcbNetDevice::m_snifferTrace),
                            "ns3::Packet::TracedCallback");

    return tid;
}

DcbNetDevice::DcbNetDevice()
    : m_node(nullptr),
      m_ifIndex(0x7fffffff),
      m_linkUp(false),
      m_mtu(DEFAULT_MTU),
      m_tInterframeGap(),
      m_channel(0),
      m_queue(nullptr),
      m_fcEnabled(false),
      m_queueDisc(nullptr),
      m_receiveErrorModel(nullptr),
      m_txMachineState(READY),
      m_currentPkt(nullptr)
{
    NS_LOG_FUNCTION(this);
}

DcbNetDevice::~DcbNetDevice()
{
    NS_LOG_FUNCTION(this);
}

void
DcbNetDevice::DoDispose()
{
    m_node = 0;
    m_channel = 0;
    m_receiveErrorModel = 0;
    m_currentPkt = 0;
    m_queue = 0;
    NetDevice::DoDispose();
}

void
PrintRawPacket(Ptr<Packet> p)
{
    uint32_t sz = p->GetSerializedSize();
    uint8_t* buffer = new uint8_t[sz];
    p->Serialize(buffer, sz);
    printf("Raw packet:");
    for (uint32_t i = 0; i < sz; i++)
    {
        if (i % 16 == 0)
        {
            printf("\n");
        }
        printf("%02x ", buffer[i]);
    }
}

void
DcbNetDevice::Receive(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet))
    {
        //
        // If we have an error model and it indicates that it is time to lose a
        // corrupted packet, don't forward this packet up, let it go.
        //
        m_phyRxDropTrace(packet);
    }
    else
    {
        // Hit the trace hooks.  All of these hooks are in the same place in this
        // device because it is so simple, but this is not usually the case in
        // more complicated devices.
        //
        m_snifferTrace(packet);
        m_phyRxEndTrace(packet);

        Ptr<Packet> originalPacket = packet->Copy();

        EthernetHeader ethHeader;
        packet->RemoveHeader(ethHeader);
        uint16_t protocol = ethHeader.GetLengthType();

        //
        // Trace sinks will expect complete packets, not packets without some of the
        // headers.
        //

        m_macRxTrace(originalPacket);
        m_rxCallback(this,
                     packet,
                     protocol,
                     GetRemote()); // calling Node::NonPromiscReceiveFromDevice
    }
}

bool
DcbNetDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << packet << dest << protocolNumber);
    NS_LOG_LOGIC("p=" << packet << ", dest=" << &dest);
    NS_LOG_LOGIC("UID is " << packet->GetUid());

    //
    // If IsLinkUp() is false it means there is no channel to send any packet
    // over so we just hit the drop trace on the packet and return an error.
    //
    if (IsLinkUp() == false)
    {
        m_macTxDropTrace(packet);
        return false;
    }

    //
    // Stick a point to point protocol header on the packet in preparation for
    // shoving it out the door.
    //

    AddEthernetHeader(packet, protocolNumber);

    m_macTxTrace(packet);

    //
    // We should enqueue and dequeue the packet to hit the tracing hooks.
    //
    if (m_queue->Enqueue(packet))
    {
        //
        // If the channel is ready for transition we send the packet right now
        //
        if (m_txMachineState == READY)
        {
            packet = m_queue->Dequeue();
            // m_promiscSnifferTrace (packet);
            bool ret = TransmitStart(packet);
            return ret;
        }
        return true;
    }

    // Enqueue may fail (overflow)

    m_macTxDropTrace(packet);
    return false;
}

bool
DcbNetDevice::TransmitStart(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    NS_LOG_LOGIC("UID is " << packet->GetUid() << ")");

    //
    // This function is called to start the process of transmitting a packet.
    // We need t tell the channel that we've started wiggling the wire and
    // schedule an event that will be executed when the transmission is complete.
    //

    NS_ASSERT_MSG(m_txMachineState == READY, "Must be READY to transmit");

    m_txMachineState = BUSY;
    m_currentPkt = packet;
    m_phyTxBeginTrace(m_currentPkt);
    m_phyTxBeginWithIdTrace(m_currentPkt, GetNodeAndPortId());
    m_snifferTrace(packet);

    Time txTime = m_bps.CalculateBytesTxTime(packet->GetSize());
    Time txCompleteTime = txTime + m_tInterframeGap;

    NS_LOG_LOGIC("Schedule TransmitCompleteEvent in " << txCompleteTime.As(Time::S));
    Simulator::Schedule(txCompleteTime, &DcbNetDevice::TransmitComplete, this);

    bool result = m_channel->TransmitStart(packet, this, txTime);
    if (result == false)
    {
        m_phyTxDropTrace(packet);
    }
    return result;
}

void
DcbNetDevice::TransmitComplete(void)
{
    NS_LOG_FUNCTION(this);

    //
    // This function is called to when we're all done transmitting a packet.
    // We try and pull another packet off of the transmit queue.  If the queue
    // is empty, we are done, otherwise we need to start transmitting the
    // next packet.
    //
    NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
    m_txMachineState = READY;

    NS_ASSERT_MSG(m_currentPkt != nullptr, "DcbNetDevice::TransmitComplete(): m_currentPkt zero");

    m_phyTxEndTrace(m_currentPkt);
    m_currentPkt = 0;

    Ptr<Packet> p = m_queue->Dequeue();
    if (m_fcEnabled)
    {
        // We let the egress buffer, which is a PausableQueueDisc, to pop one packet at
        // a time and send out through this device. As a result, at most one data packet
        // should be in the m_queue.
        // If device queue has packets left, they must be control frames. Send them
        // immedidately.
        if (p)
        {
            TransmitStart(p);
        }

        /**
         * If the device queue is empty, make queueDisc to send one packet down.
         * Should not do this procedure every time when a packet is sent out, given
         * that the FC Frame is sent out through this device as well.
         */
        if (m_queue->GetCurrentSize() == QueueSize("0p"))
        {
            m_queueDisc->RunEnd(); // finish the run
            // Ask the egress buffer to pop next packet if there is any packet not paused.
            m_queueDisc->Run();
        }
        else if (m_queue->GetCurrentSize() >= QueueSize("2p"))
        {
            NS_LOG_DEBUG("Device queue is not empty after transmitting a packet. Current size: "
                         << m_queue->GetCurrentSize() << "");
        }
    }
    else
    {
        if (p == nullptr)
        {
            NS_LOG_LOGIC("No pending packets in device queue after tx complete");
            return;
        }
        //
        // Got another packet off of the queue, so start the transmit process again.
        //
        TransmitStart(p);
    }
}

Address
DcbNetDevice::GetRemote(void) const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_channel->GetNDevices() == 2);
    for (std::size_t i = 0; i < m_channel->GetNDevices(); ++i)
    {
        Ptr<NetDevice> tmp = m_channel->GetDevice(i);
        if (tmp != this)
        {
            return tmp->GetAddress();
        }
    }
    NS_ASSERT(false);
    // quiet compiler.
    return Address();
}

void
DcbNetDevice::SetDataRate(DataRate bps)
{
    NS_LOG_FUNCTION(this);
    m_bps = bps;
}

DataRate
DcbNetDevice::GetDataRate() const
{
    NS_LOG_FUNCTION(this);
    return m_bps;
}

void
DcbNetDevice::SetInterframeGap(Time t)
{
    NS_LOG_FUNCTION(this << t.As(Time::S));
    m_tInterframeGap = t;
}

void
DcbNetDevice::AddEthernetHeader(Ptr<Packet> p, uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << p << protocolNumber);
    EthernetHeader eth;
    eth.SetSource(Mac48Address::ConvertFrom(GetAddress()));
    eth.SetDestination(Mac48Address::ConvertFrom(GetRemote()));
    eth.SetLengthType(protocolNumber);
    p->AddHeader(eth);
}

Ptr<Node>
DcbNetDevice::GetNode(void) const
{
    return m_node;
}

void
DcbNetDevice::SetNode(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this);
    m_node = node;
}

void
DcbNetDevice::SetAddress(Address address)
{
    m_address = Mac48Address::ConvertFrom(address);
}

Address
DcbNetDevice::GetAddress(void) const
{
    return m_address;
}

bool
DcbNetDevice::Attach(Ptr<DcbChannel> ch)
{
    NS_LOG_FUNCTION(this << &ch);

    m_channel = ch;

    m_channel->Attach(this);

    //
    // This device is up whenever it is attached to a channel.  A better plan
    // would be to have the link come up when both devices are attached, but this
    // is not done for now.
    //
    NotifyLinkUp();
    return true;
}

void
DcbNetDevice::SetQueue(Ptr<Queue<Packet>> q)
{
    NS_LOG_FUNCTION(this << q);
    m_queue = q;
}

bool
DcbNetDevice::IsLinkUp(void) const
{
    NS_LOG_FUNCTION(this);
    return m_linkUp;
}

void
DcbNetDevice::NotifyLinkUp()
{
    NS_LOG_FUNCTION(this);
    m_linkUp = true;
}

bool
DcbNetDevice::NeedsArp(void) const
{
    NS_LOG_FUNCTION(this);
    return false;
}

void
DcbNetDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb)
{
    m_rxCallback = cb;
}

bool
DcbNetDevice::SetMtu(uint16_t mtu)
{
    NS_LOG_FUNCTION(this << mtu);
    m_mtu = mtu;
    return true;
}

uint16_t
DcbNetDevice::GetMtu(void) const
{
    NS_LOG_FUNCTION(this);
    return m_mtu;
}

void
DcbNetDevice::SetIfIndex(const uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_ifIndex = index;
}

uint32_t
DcbNetDevice::GetIfIndex(void) const
{
    NS_ASSERT_MSG(m_ifIndex < 0x7fffffff, "DcbNetDevice index not set");
    return m_ifIndex;
}

Ptr<Channel>
DcbNetDevice::GetChannel(void) const
{
    return m_channel;
}

//
// This is a point-to-point device, so every transmission is a broadcast to
// all of the devices on the network.
//
bool
DcbNetDevice::IsBroadcast(void) const
{
    NS_LOG_FUNCTION(this);
    return true;
}

Address
DcbNetDevice::GetBroadcast(void) const
{
    NS_LOG_FUNCTION(this);
    return Mac48Address("ff:ff:ff:ff:ff:ff");
}

bool
DcbNetDevice::IsMulticast(void) const
{
    NS_LOG_FUNCTION(this);
    return true;
}

Address
DcbNetDevice::GetMulticast(Ipv4Address multicastGroup) const
{
    NS_LOG_FUNCTION(this);
    return Mac48Address("01:00:5e:00:00:00");
}

Address
DcbNetDevice::GetMulticast(Ipv6Address addr) const
{
    NS_LOG_FUNCTION(this << addr);
    return Mac48Address("33:33:00:00:00:00");
}

bool
DcbNetDevice::IsPointToPoint(void) const
{
    NS_LOG_FUNCTION(this);
    return true;
}

bool
DcbNetDevice::IsBridge(void) const
{
    NS_LOG_FUNCTION(this);
    return false;
}

void
DcbNetDevice::AddLinkChangeCallback(Callback<void> callback)
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("DcbNetDevice::AddLinkChangeCallback () not implemented");
}

void
DcbNetDevice::SetPromiscReceiveCallback(PromiscReceiveCallback cb)
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("DcbNetDevice::SetPromiscReceiveCallback () not implemented");
}

bool
DcbNetDevice::SupportsSendFrom(void) const
{
    NS_LOG_FUNCTION(this);
    return false;
}

void
DcbNetDevice::SetQueueDisc(Ptr<PausableQueueDisc> queueDisc)
{
    NS_LOG_FUNCTION(this << queueDisc);
    m_queueDisc = queueDisc;
}

Ptr<PausableQueueDisc>
DcbNetDevice::GetQueueDisc() const
{
    NS_LOG_FUNCTION(this);
    return m_queueDisc;
}

void
DcbNetDevice::SetFcEnabled(bool enabled)
{
    NS_LOG_FUNCTION(this << enabled);
    m_fcEnabled = enabled;
}

bool
DcbNetDevice::SendFrom(Ptr<Packet> packet,
                       const Address& source,
                       const Address& dest,
                       uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << packet << source << dest << protocolNumber);
    return false;
}

std::pair<uint32_t, uint32_t>
DcbNetDevice::GetNodeAndPortId() const
{
    return std::make_pair(m_node->GetId(), m_ifIndex);
}

} // namespace ns3
