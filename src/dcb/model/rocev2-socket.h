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

#ifndef ROCEV2_SOCKET_H
#define ROCEV2_SOCKET_H

#include "rocev2-congestion-ops.h"
#include "rocev2-oscar.h"
#include "rocev2-prioplus-ledbat.h"
#include "rocev2-prioplus-swift.h"
#include "udp-based-socket.h"

#include "ns3/ipv4-address.h"
#include "ns3/rocev2-header.h"
#include "ns3/traced-callback.h"

namespace ns3
{

class RoCEv2CongestionOps;
class RoCEv2SocketState;
class IrnHeader;

enum RoCEv2RetxMode : uint8_t
{
    GBN, // Go Back N
    IRN,  // Improved RoCE NIC, see Revisiting Network Support for RDMA by Mittal et al.
    NONE // No retransmission, for packet spraying only.
};

class DcbTxBuffer : public Object
{
  public:
    struct DcbTxBufferItem
    {
        DcbTxBufferItem(uint32_t psn,
                        RoCEv2Header header,
                        Ptr<Packet> p,
                        Ipv4Address daddr,
                        Ptr<Ipv4Route> route);

        uint32_t m_psn;
        RoCEv2Header m_header;
        Ptr<Packet> m_payload;
        Ipv4Address m_daddr;
        Ptr<Ipv4Route> m_route;

    }; // class DcbTxBufferItem

    enum TxPacketState : uint8_t
    {
        UNACK, // already been sent but not been acked
        NACK,  // already been retransmitted
        ACK,   // acked
        UNDEF  // out of range
    };

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    // No default constructor, as we need the sendCb
    DcbTxBuffer(Callback<void> sendCb, Callback<RoCEv2Header> createRocev2HeaderCb);

    typedef std::deque<DcbTxBufferItem>::const_iterator DcbTxBufferItemI;

    void Push(uint32_t psn,
              RoCEv2Header header,
              Ptr<Packet> payload,
              Ipv4Address daddr,
              Ptr<Ipv4Route> route);
    /**
     * \brief Get the front item of the buffer.
     */
    const DcbTxBufferItem& Front() const;
    // We do not need pop, to avoid mistakes
    /**
     * \brief Remove the item to given PSN.
     * \param psn PSN which has been acknowledged.
     */
    void AcknowledgeTo(uint32_t psn);
    /**
     * \brief Acknowledge the given PSN.
     * In this function, we just set m_acked[psn] to true.
     *
     * \param psn PSN which has been acknowledged.
     */
    void Acknowledge(uint32_t psn);
    /**
     * \brief Check if some packets can be released.
     */
    void CheckRelease();
    /**
     * \brief Return the next psn to be sent. If there is no packet to be sent, return TotalSize().
     */
    uint32_t NextSendPsn();
    /**
     * \brief Remove the next item to be sent.
     */
    const DcbTxBufferItem& PopNextShouldSend();
    /**
     * \brief Return whether cwnd is enough to send a packet.
     * \param cwnd unit: packets
     */
    inline bool CouldSend(uint64_t cwnd);
    /**
     * \brief Return whether cwnd is enough to send a packet with PSN.
     * \param cwnd unit: packets
     */
    inline bool CouldSend(uint32_t psn, uint64_t cwnd);
    /**
     * \brief Return the number of inflight packets.
     * \param reorder whether assuming the packets can be reordered.
     */
    uint32_t InflightPkts(bool reorder = false);
    /**
     * \brief Return the number of packets in the buffer.
     */
    uint32_t Size() const;
    /**
     * \brief Return the number of packets should be sent.
     */
    uint32_t TotalSize() const;
    /**
     * \brief Number of packets left to be sent
     */
    uint32_t GetSizeToBeSent();
    /**
     * \brief Add a PSN to the txQueue.
     * Called when a packet with the PSN is desired to be retransmit.
     */
    void Retransmit(uint32_t psn);
    /**
     * \brief Add a range of PSN to the txQueue.
     * Called when a range of packets with the PSNs is desired to be retransmit.
     */
    void RetransmitRange(uint32_t from, uint32_t to);
    /**
     * @brief Get the number of On The Fly packets.
     *
     * @return uint32_t the number of On The Fly packets.
     */
    uint32_t GetOnTheFly();
    /**
     * @brief Get the Max Acked Psn.
     *
     * @return uint32_t
     */
    uint32_t GetMaxAckedPsn();
    /**
     * @brief Set the Pkt State of all pkt in range of [from, to)
     *
     * @param from
     * @param to
     * @param state
     */
    void SetPktState(uint32_t from, uint32_t to, uint8_t state);
    /**
     * \brief Retansmit all packets from the given PSN, i.e., go back N.
     * \return the number of packets to be retransmitted.
     */
    void RetransmitFrom(uint32_t psn);
    /**
     * \brief Get the PSN of the front item in the buffer.
     */
    uint32_t GetFrontPsn() const;
    /**
     * \brief Whether the buffer has gap.
     */
    bool HasGap() const;

    /**
     * \brief receive a payload from upper layer.
     */
    void RecvPayload(Ptr<Packet> payload, Ipv4Address daddr, Ptr<Ipv4Route> route, uint32_t mss);

    /**
     * \brief Get the remain size to be sent. Unit: packet
     */
    uint32_t RemainSizeInPacket() const;
    /**
     * \brief Get the remain size to be sent. Unit: byte
     */
    uint32_t RemainSizeInBytes() const;

    /**
     * \brief Create a packet and push it into the buffer.
     */
    void CreatePacket(RoCEv2Header roceHeader);

    /**
     * \brief Get the packet with the given PSN.
     *
     * If the packet is not in the buffer, create it and push it into the buffer.
     */
    const DcbTxBufferItem& GetPacketAt(uint32_t psn);

    DcbTxBufferItemI FindPSN(uint32_t psn) const;
    DcbTxBufferItemI End() const;

    /**
     * \brief Clear the payload. No matter whether the payload has been sent.
     */
    void ClearPayload();
    /**
     * \brief Clear all psn >= given psn in the txQueue. Used to terminate the socket.
     */
    void ClearTxQueue(uint32_t psn);

  protected:
    /**
     * \brief Get the next item to be sent.
     * Calling this function should ensure that there is a packet created but not sent.
     */
    const DcbTxBufferItem& PeekNextShouldSend();

  private:
    /**
     * \brief The SendPendingPacket() call back, called every time the tx buffer has new packet to
     * send.
     */
    Callback<void> m_sendCb;

    /**
     * \brief The CreateRoCEv2Header() call back, called when a new packet is created.
     *
     * Note that this callback should be called only once for a psn.
     */
    Callback<RoCEv2Header> m_createRocev2HeaderCb;

    std::deque<DcbTxBufferItem> m_buffer;
    uint32_t m_frontPsn; // PSN of the front item in the buffer. Only increase when acked.
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>>
        m_txQueue;             // PSN of the items to be sent
    std::vector<bool> m_acked; // Whether the packet with the PSN is acked, serve as a bitmap in
                               // retx mode IRN. The index is the PSN.
    std::vector<uint8_t> m_pktState; // the state of a packet(ack, unack, lost, undef)
    uint32_t m_maxAckedPsn;          // The max PSN which has been acknowledged, used to detect gap.

    uint32_t m_remainSize;  // The size of data to be sent
    uint32_t m_maxSentPsn;  // The max PSN has been sent
    Ipv4Address m_daddr;    // The destination address to send the packet
    Ptr<Ipv4Route> m_route; // The route to send the packet
    uint32_t m_mss;

    Ptr<Packet> m_payload; // The payload to be sent
}; // class DcbTxBuffer

class DcbRxBuffer : public Object
{
  public:
    struct DcbRxBufferItem
    {
        // No default constructor
        DcbRxBufferItem(Ipv4Header ipv4H, RoCEv2Header roceH, Ptr<Packet> p);

        Ipv4Header m_ipv4H;
        RoCEv2Header m_roceH;
        Ptr<Packet> m_payload;
    }; // class DcbTxBufferItem

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    // No default constructor
    DcbRxBuffer(Callback<void, Ptr<Packet>, Ipv4Header, uint32_t, Ptr<Ipv4Interface>> forwardCb,
                Ptr<Ipv4Interface> incomingInterface,
                RoCEv2RetxMode retxMode);

    /**
     * \brief Add a packet into the buffer.
     */
    void Add(uint32_t psn, Ipv4Header ipv4, RoCEv2Header roce, Ptr<Packet> payload);

    uint32_t GetExpectedPsn() const;

  private:
    /**
     * \brief The ForwardUp() call back, called when has packet to be forwarded up.
     */
    Callback<void, Ptr<Packet>, Ipv4Header, uint32_t, Ptr<Ipv4Interface>> m_forwardCb;
    std::map<uint32_t, DcbRxBufferItem> m_buffer;
    uint32_t m_expectedPsn; // PSN of the front item in the buffer. Only increase when ForwardUp.
    Ptr<Ipv4Interface> m_forwardInterface; // Interface to forward the packet up
    RoCEv2RetxMode m_retxMode;
}; // class DcbRxBuffer

class RoCEv2SocketState : public Object
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2SocketState();

    inline void SetRateRatioPercent(double ratio)
    {
        ratio = CheckRateRatio(ratio);
        m_rateRatio = ratio;
    }

    inline double GetRateRatioPercent() const
    {
        return m_rateRatio;
    }

    inline void SetMinRateRatio(double ratio)
    {
        m_minRateRatio = ratio;
    }

    inline double GetMinRateRatio() const
    {
        return m_minRateRatio;
    }

    inline void SetCwnd(uint64_t cwnd)
    {
        m_cwnd = cwnd;
    }

    inline uint64_t GetCwnd() const
    {
        return m_cwnd;
    }

    inline void SetTxBuffer(DcbTxBuffer* txBuffer)
    {
        m_txBuffer = txBuffer;
    }

    inline DcbTxBuffer* GetTxBuffer() const
    {
        return m_txBuffer;
    }

    inline void SetDeviceRate(DataRate* deviceRate)
    {
        m_deviceRate = deviceRate;
    }

    inline DataRate* GetDeviceRate() const
    {
        return m_deviceRate;
    }

    inline void SetBaseRtt(Time baseRtt)
    {
        m_baseRtt = baseRtt;
    }

    inline Time GetBaseRtt() const
    {
        return m_baseRtt;
    }

    /**
     * \brief Get the base BDP(Bytes) using device rate and base RTT.
     */
    inline uint64_t GetBaseBdp() const
    {
        if (m_deviceRate != nullptr)
        {
            return static_cast<uint64_t>(m_deviceRate->GetBitRate() / 8 * m_baseRtt.GetSeconds());
        }
        else
        {
            return 0;
        }
    }

    inline void SetBaseOneWayDelay(Time baseOneWayDelay)
    {
        m_baseOneWayDelay = baseOneWayDelay;
    }

    inline Time GetBaseOneWayDelay() const
    {
        return m_baseOneWayDelay;
    }

    inline void SetPacketSize(uint32_t packetSize)
    {
        m_packetSize = packetSize;
    }

    inline uint32_t GetPacketSize() const
    {
        return m_packetSize;
    }

    inline void SetMss(uint32_t mss)
    {
        m_mss = mss;
    }

    inline uint32_t GetMss() const
    {
        return m_mss;
    }

    inline void SetFlowTotalSize(uint64_t flowTotalSize)
    {
        m_flowTotalSize = flowTotalSize;
    }

    inline uint64_t GetFlowTotalSize() const
    {
        return m_flowTotalSize;
    }

    /**
     * \brief Check if rateRatio is less than 1.0 and greater than m_minRateRatio.
     * if not, return the corrected rateRatio.
     * \return the corrected rateRatio.
     */
    inline double CheckRateRatio(double rateRatio) const
    {
        return std::min(std::max(rateRatio, m_minRateRatio), 1.);
    }

    inline double GetInflightRatio() const
    {
        // Get the inflight pkts
        return (double)m_txBuffer->InflightPkts() / ((double)m_cwnd / m_packetSize);
    }

    bool m_receivedEcn; //!< Marking whether the last packet received is ECN marked
  private:
    /**
     * Instead of directly store sending rate here, we store a rate ratio.
     * rateRatio = target sending rate / link rate * 100.0 .
     * In this way, this class is totally decoupled with others.
     */
    double m_rateRatio;
    double m_minRateRatio;
    uint64_t m_cwnd; //!< unit: bytes
    DcbTxBuffer* m_txBuffer;
    DataRate* m_deviceRate;
    Time m_baseRtt; //!< Base RTT. Note that this is auto set by DcbDcbTrafficGenApplication. If
                    //!< not, it should be set manually.
    Time m_baseOneWayDelay;   //!< Base one way delay. Note that this is auto set by
                              //!< DcbDcbTrafficGenApplication. If not, it should be set manually.
    uint32_t m_packetSize;    //!< MSS + headersize
    uint32_t m_mss;           //!< MSS
    uint64_t m_flowTotalSize; //!< The total size of the flow

}; // class RoCEv2SocketState

class RoCEv2Socket : public UdpBasedSocket
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2Socket();
    ~RoCEv2Socket() override;

    int Bind() override;
    void BindToNetDevice(Ptr<NetDevice> netdevice) override;

    int BindToLocalPort(uint32_t port);

    void SetStopTime(Time stopTime); // for RoCEv2 Congestion

    // void SetCcOps(Ptr<RoCEv2CongestionOps> algo);
    void SetCcOps(TypeId congTypeId);
    void SetCcOps(TypeId congTypeId, std::vector<RoCEv2CongestionOps::CcOpsConfigPair_t>& ccConfig);

    /**
     * \brief Set the RoCEv2SocketState's BaseRtt and BaseOneWayDelay
     * \param delay propogation delay
     */
    void SetBaseRttNOneWayDelay(uint32_t hop, Time delay, uint32_t packetSize, uint32_t ackSize);

    Time GetCNPInterval() const;

    Time GetFlowStartTime() const;

    /**
     * \brief get the srcPort/dstPort of this socket, only be called after call Bind()!
     * used for generate RoceV2Header to find the outport in srcNode.
     * \return the srcPort/dstPort of this socket.
     */
    uint32_t GetSrcPort() const override;
    uint32_t GetDstPort() const override;

    /**
     * \brief Get the congestion control algorithm.
     */
    Ptr<RoCEv2CongestionOps> GetCcOps() const;

    /**
     * \brief Get the socket state.
     */
    Ptr<RoCEv2SocketState> GetSocketState() const;

    /**
     * \brief Terminate the socket sending. That is, stop the socket from sending new packet.
     */
    void Terminate();

    // \brief Structure that keeps the RoCEv2Socket statistics
    class Stats
    {
      public:
        // constructor
        Stats();

        uint32_t nTotalSizePkts; //<! Data size sent by upper layer
        uint64_t nTotalSizeBytes;
        uint32_t nTotalSentPkts; //<! Data pkts sent to lower layer, including retransmission
        uint64_t nTotalSentBytes;
        uint32_t nTotalDeliverPkts; //<! Data pkts successfully delivered to peer upper layer
        uint64_t nTotalDeliverBytes;
        /**
         * No need to record the number of lost packets, as it can be calculated by
         * nTotalSentPkts - nTotalDeliverPkts. And it is often not accurate, as the lost packets can
         * not be detected at sender in some cases.
         */
        uint32_t nRetxCount; //<! Number of retransmission
        Time tStart;
        Time tFinish;
        Time tFct;                //<! Flow completion time
        DataRate overallFlowRate; //<! overall rate, calculate by total size / (first msg arrive -
                                  // last msg finish)

        // Detailed sender statistics, only enabled if needed
        bool bDetailedSenderStats;
        std::vector<std::pair<Time, DataRate>> vCcRate; //<! Record the rate when changed
        std::vector<std::pair<Time, uint32_t>> vCcCwnd; //<! Record the cwnd when changed
        std::vector<Time> vRecvEcn;                     //<! Record the time when received ECN
        std::vector<std::pair<Time, uint32_t>>
            vSentPkt; //<! Record the packets' send time and size XXX (only payload now)

        // Detailed statistics for retx, only enabled if needed
        bool bDetailedRetxStats;
        std::vector<std::pair<Time, uint32_t>> vSentPsn;  //<! Record the packets' send time and PSN
        std::vector<std::pair<Time, uint32_t>> vAckedPsn; //<! Record the ack recv time and PSN
        std::vector<std::pair<Time, uint32_t>> vExpectedPsn; //<! Record the ack recv time and PSN

        std::string flowTag; //<! The tag of the flow to identify it, configured by the application

        std::shared_ptr<RoCEv2CongestionOps::Stats> ccStats;

        // Recorder function of the detailed statistics
        void RecordCcRate(DataRate rate);
        void RecordCcCwnd(uint32_t cwnd);
        void RecordRecvEcn();
        void RecordSentPkt(uint32_t size);
        void RecordSentPsn(uint32_t psn);
        void RecordAckedPsn(uint32_t psn);
        void RecordExpectedPsn(uint32_t psn);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<Stats> GetStats() const;

    /**
     * \param ipTos the TOS value (in the range 0..255)
     * \return The priority value corresponding to the given TOS value
     */
    static uint8_t IpTos2Priority(uint8_t ipTos);

  protected:
    void DoSendTo(Ptr<Packet> p, Ipv4Address daddr, Ptr<Ipv4Route> route) override;

    /**
     * \brief Try to send a pending packet.
     *
     * Try to send a pending packet. Usually called when a new packet can be sent.
     * The function will check if it is allowed to send a packet, and if so,
     * call DoSendDataPacket to send a data packet.
     */
    void SendPendingPacket();

    /**
     * \brief Actually send out a data packet.
     *
     * The packet to be sent is controled by m_buffer. This function just call m_buffer
     * and send the packet out.
     */
    void DoSendDataPacket(const DcbTxBuffer::DcbTxBufferItem& item);

    void ForwardUp(Ptr<Packet> packet,
                   Ipv4Header header,
                   uint32_t port,
                   Ptr<Ipv4Interface> incomingInterface) override;

    /**
     * \brief Called by flow scheduler at RoCEv2L4Proto to notify the socket to send a packet down.
     */
    void NotifyCouldSend();

    /**
     * \brief Called when the flow is finished, do some clean and record work.
     */
    void Finish();

  private:
    struct FlowInfo // for receiver
    {
        uint32_t dstQP;
        uint32_t nextPSN;
        bool receivedECN;
        EventId lastCNPEvent;
        DcbRxBuffer m_rxBuffer;
        Ptr<RoCEv2CongestionOps> m_ccOps;
        /**
         * In RoCE, receiver will only generate one NACK for a expected PSN. Thus we use a bool to
         * record whether the expected PSN advanced after a NACK. If true, it is permitted to send a
         * new NACK.
         */
        bool m_ePsnAdvancedAfterNack; // Whether the expected PSN advanced after a NACK

        FlowInfo(uint32_t dst, DcbRxBuffer rxBuffer, Ptr<RoCEv2CongestionOps> ccOps)
            : dstQP(dst),
              nextPSN(0),
              receivedECN(false),
              m_rxBuffer(rxBuffer),
              m_ccOps(ccOps),
              m_ePsnAdvancedAfterNack(false)
        {
        }

        uint32_t GetExpectedPsn() const
        {
            return m_rxBuffer.GetExpectedPsn();
        }
    };

    typedef std::pair<Ipv4Address, uint32_t> FlowIdentifier;

    RoCEv2Header CreateNextProtocolHeader();
    void HandleACK(Ptr<Packet> packet, const RoCEv2Header& roce);
    void HandleDataPacket(Ptr<Packet> packet,
                          Ipv4Header header,
                          uint32_t port,
                          Ptr<Ipv4Interface> incomingInterface,
                          const RoCEv2Header& roce);

    void GoBackN(uint32_t lostPSN);
    /**
     * \brief React to the ACK packet in IRN mode.
     */
    // void IrnReactToAck(uint32_t expectedPSN);
    /**
     * \brief React to the NACK packet in IRN mode.
     */
    void IrnReactToNack(uint32_t expectedPSN, IrnHeader irnH);

    void ScheduleNextCNP(std::map<FlowIdentifier, FlowInfo>::iterator flowInfoIter,
                         Ipv4Header header);
    /**
     * \brief Check whether the given priority queue disc avaliable to buffer more packet.
     *
     * Get the size of the given priority queue disc, and check if it is lower than a threshold.
     * If so, return true, otherwise return false. In this way, sockets can avoid to overwhelm
     * the queue disc. As well as avoid to send too many uncontrolled packet in the queue disc.
     *
     * This function only work with dcb dev and pausable queue disc, if not, it will always return
     * true.
     */
    bool CheckQueueDiscAvaliable(uint8_t priority) const;
    /**
     * \brief Check whether the queue disc of control priority avaliable to buffer more packet.
     *
     * Throw fatal error if it is inavaliable as control packet should always be sent.
     *
     * This function only work with dcb dev and pausable queue disc, if not, it will always return
     * true.
     */
    void CheckControlQueueDiscAvaliable() const;

    /**
     * \brief Do forward up to upper layer.
     *
     * In this function, the UdpBasedSocket::ForwardUp() is called.
     */
    void DoForwardUp(Ptr<Packet> packet,
                     Ipv4Header header,
                     uint32_t port,
                     Ptr<Ipv4Interface> incomingInterface);

    // Time CalcTxTime (uint32_t bytes);

    std::shared_ptr<Stats> m_stats;

    TypeId m_congTypeId;                //!< TypeId of congestion control algorithm
    Ptr<RoCEv2CongestionOps> m_ccOps;   //!< RoCEv2 congestion control
    Ptr<RoCEv2SocketState> m_sockState; //!< socket state
    DcbTxBuffer m_txBuffer;
    DataRate m_deviceRate;
    // bool m_isSending;
    EventId m_sendEvent; //!< Event id of the next send event

    uint32_t m_senderNextPSN; //!< Note that it is not the PSN of the next
                              //!< packet to be sent by socket, which is the top of
                              //!< m_txBuffer.m_txQueue.
    std::map<FlowIdentifier, FlowInfo> m_receiverFlowInfo;
    uint32_t m_psnEnd; //!< the last PSN + 1, used to check if flow completes

    Time m_CNPInterval; //!< Interval to send CNP
    Time m_flowStartTime;

    RoCEv2RetxMode m_retxMode;

    uint32_t m_innerPrio; //!< The inner priority of the sockets when contenting with other sockets

    DataRate m_rateCap; //!< Rate cap, used to run some experiments

    /***** Utility for Probe (Like DOCA's RTT Req) *****/
    /**
     * \brief Send a probe packet, now only used in PrioPlus.
     *
     * \return If the packet is sent, return true, otherwise return false.
     */
    bool SendProbePacket(uint32_t psn);
    /**
     * \brief Handle the probe packet, now only used in PrioPlus.
     *
     * Handle the income probe packet and return a probe ACK packet.
     */
    void HandleProbePacket(Ptr<Packet> packet,
                           Ipv4Header header,
                           uint32_t port,
                           Ptr<Ipv4Interface> incomingInterface,
                           const RoCEv2Header& roce);
    /**
     * \brief Send a probe ACK packet, called when a probe packet is received.
     */
    void SendProbeAckPacket(Ptr<Packet> packet,
                            Ipv4Header header,
                            uint32_t port,
                            Ptr<Ipv4Interface> incomingInterface,
                            const RoCEv2Header& roce);
    /**
     * \brief Handle the probe packet, now only used in PrioPlus.
     */
    Callback<void, Ptr<Packet>> m_probeRecvCb;

    /***** Utility for FlowScheduling *****/
    bool m_waitingForSchedule; //!< Whether the socket is waiting for the flow scheduler to notify
                               //!< it to send a packet down.

    /***** Utility for RTO *****/
    /**
     * \brief Retransmission timeout event.
     *
     * Retransmit according to the retransmission mode. For GBN, retransmit all packets from the
     * first unacked packet. For IRN, retransmit the first unacked packet.
     */
    void RetransmissionTimeout();
    /**
     * @brief Get the RTO time for IRN Timer.
     * GBN return m_rto, in IRN when the number of on-the-fly pakcts less than m_irnPktThresh return
     * m_irnRtoLow.
     *
     * @return Time
     */
    Time GetRTOTime();
    Time m_rto;         //!< Retransmission timeout (RTOhigh when IRN is used)
    EventId m_rtoEvent; //!< Event id of the next RTO event
    Time m_lastRto;     //!< The time of schedule a rto event

    Time m_irnRtoLow;        //!< Low retransmission timeout for IRN
    uint32_t m_irnPktThresh; //!< The threshold of the number of packets to trigger low RTO for IRN

    enum FlowState
    {
        PENDING,
        RUNNING,
        FINISHED
    };

    FlowState m_flowState;

    /***** Utility for Ack-driven pacing when cwnd < 1pkt *****/
    bool m_ackDrivenPacing; //!< Whether the socket is in ack-driven pacing mode
    // XXX record m_lastAckTime when ack recved not txbuffer moved, may have problem in lossy
    // environment
    Time m_lastAckTime; //!< The time of the last ack
}; // class RoCEv2Socket

/**
 * \brief Header for IRN.
 * It will be repleaced after AETH header in the NACK packet.
 * It only has acked PSN. As the expected PSN is in the AETH header.
 */
class IrnHeader : public Header
{
  public:
    IrnHeader();
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;
    void SetAckedPsn(uint32_t psn);
    uint32_t GetAckedPsn() const;

  private:
    uint32_t m_ackedPsn;
}; // class IrnHeader

class ProbePacketTag : public Tag
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer buf) const override;
    void Deserialize(TagBuffer buf) override;
    void Print(std::ostream& os) const override;
    ProbePacketTag();
    ProbePacketTag(bool isProbe);

  private:
    bool m_isProbe; //!< if true, the packet is a probe packet, otherwise, it is a ACK of probe
                    //!< packet
};


} // namespace ns3

//
#endif // ROCEV2_SOCKET_H
