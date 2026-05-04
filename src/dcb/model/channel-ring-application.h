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
 */

#ifndef DCB_TRAFFIC_GEN_APPLICATION_H
#define DCB_TRAFFIC_GEN_APPLICATION_H

#include "dcb-base-application.h"
#include "dcb-net-device.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/dc-topology.h"
#include "ns3/flow-identifier.h"
#include "ns3/inet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rocev2-header.h"
#include "ns3/seq-ts-size-header.h"
#include "ns3/traced-callback.h"

#include <map>
#include <set>
#include <string>

namespace ns3
{

class Socket;

class ChannelRingTag : public Tag
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
    ChannelRingTag();

    /**
     * Constructs a RealTimeStatsTag with the given seq
     */
    ChannelRingTag(uint8_t channel, uint8_t chunk);

    uint8_t GetChannel() const;
    uint8_t GetChunk() const;
    void SetDest(uint8_t dest);
    uint8_t GetDest() const;

    void SetPassed(uint8_t passedNode);
    bool IsPassed(uint8_t node) const;
    bool AllPassed() const;

    bool operator<(const ChannelRingTag& other) const;

  private:
    uint8_t m_channel; // The channel id the packet belongs to
    uint8_t m_chunk;   // The chunk id the packet belongs to
    uint8_t m_dest;    // The destination node
    uint32_t m_passed; // The passed node in onehot encoding
};

/**
 * \ingroup dcb
 * \brief A application that generates traffic and sends it to a destination.
 */
class ChannelRingApplication : public DcbBaseApplication
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Create an application in topology node nodeIndex.
     * The application will randomly choose a node as destination and send flows.
     */
    // ChannelRingApplication (Ptr<DcTopology> topology, uint32_t nodeIndex);

    /**
     * \brief Create an application in topology node nodeIndex destined to destIndex.
     * The application will send flows from nodeIndex to destIndex.
     * * If the destIndex is negative, the application will randomly choose a node as the
     * destination.
     */
    ChannelRingApplication();
    ChannelRingApplication(Ptr<DcTopology> topology, uint32_t nodeIndex);
    virtual ~ChannelRingApplication();

    constexpr static inline const uint64_t MSS = 1000; // bytes

    struct Flow : public DcbBaseApplication::Flow
    {
        ChannelRingTag m_tag; //<! The tag of the flow

        Flow(uint64_t s, Time t, uint32_t dest, Ptr<Socket> sock, ChannelRingTag tag)
            : DcbBaseApplication::Flow(s, t, dest, sock),
              m_tag(tag)
        {
        }
    };

    enum TrafficPattern
    {
        CHANNEL_RING,
        CHANNEL_RING_OPT
    };

    class Stats : public DcbBaseApplication::Stats
    {
      public:
        // constructor
        Stats(Ptr<ChannelRingApplication> app);

        virtual ~Stats()
        {
        } // Make the base class polymorphic

        bool isCollected; //<! Whether the stats is collected

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck(std::map<Ptr<Socket>, DcbBaseApplication::Flow*> flows);

        // No getter for simplicity
    };

    virtual std::shared_ptr<DcbBaseApplication::Stats> GetStats() const;

  private:
    /**
     * \brief Init fields, e.g., RNGs and m_socketLinkRate.
     */
    virtual void InitMembers() override;

    /**
     * \brief Schedule the next flow to the destination.
     */
    void ScheduleNextFlow(ChannelRingTag tag);

    /**
     * \brief Generate traffic according to the traffic pattern.
     */
    virtual void GenerateTraffic() override;

    /**
     * \brief Calculate parameters of traffic.
     */
    virtual void CalcTrafficParameters() override;

    /**
     * \brief Handle a packet reception.
     *
     * This function is called by lower layers.
     *
     * \param socket the socket the packet was received to.
     */
    virtual void HandleRead(Ptr<Socket> socket) override;

    std::shared_ptr<Stats> m_stats;

    // std::map<Ptr<Socket>, Flow*> m_flows; // Redefine m_flows as we redefine Flow

    /*********************************
     * Members for traffic patterns
     *********************************/
    enum TrafficPattern m_trafficPattern; //!< The traffic pattern of the application
    uint64_t m_trafficSize;               //!< The total size of the traffic

    /***** Members for channel ring traffic *****/
    static const uint8_t m_numChannels = 8; //!< The number of channels, ie, nodes per server
    static const uint8_t m_numChunks = 32;  //!< The number of chunks in each channel
    static std::map<uint8_t, std::vector<uint8_t>>
        m_channelSeqences;                        //!< The sequence of node id of channels
    std::map<uint8_t, uint8_t> m_channelPrevNode; //!< The previous hop of the node in channels
    std::map<uint8_t, uint8_t> m_channelNextNode; //!< The next hop of the node in channels
    void CreateChannelRing();
    void GenerateChannelRingSequence();
    void GenerateChannelRingOptSequence();

    std::map<ChannelRingTag, uint32_t> m_recvedChannelRingChunks; //!< The flows in the channel ring

}; // class ChannelRingApplication

} // namespace ns3

#endif // DCB_TRAFFIC_GEN_APPLICATION_H
