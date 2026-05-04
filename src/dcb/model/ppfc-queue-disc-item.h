/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
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

#ifndef PPFC_QUEUE_DISC_ITEM_H
#define PPFC_QUEUE_DISC_ITEM_H

#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/packet.h"
#include "ns3/queue-item.h"

namespace ns3
{

/**
 * \ingroup dcb
 * \ingroup traffic-control
 *
 * PPfcQueueDiscItem is a subclass of Ipv4QueueDiscItem which stores the PPFC qIdx of packets.
 */
class PPfcQueueDiscItem : public Ipv4QueueDiscItem
{
  public:
    /**
     * \brief Create an IPv4 queue disc item containing an IPv4 packet.
     * \param p the packet included in the created item.
     * \param addr the destination MAC address
     * \param protocol the protocol number
     * \param header the IPv4 header
     */
    PPfcQueueDiscItem(Ptr<Packet> p,
                      const Address& addr,
                      uint16_t protocol,
                      const Ipv4Header& header,
                      uint32_t qIdx);

    ~PPfcQueueDiscItem() override;

    // Delete default constructor, copy constructor and assignment operator to avoid misuse
    PPfcQueueDiscItem() = delete;
    PPfcQueueDiscItem(const PPfcQueueDiscItem&) = delete;
    PPfcQueueDiscItem& operator=(const PPfcQueueDiscItem&) = delete;

    /**
     * \return the index of queueDis that the item should be put in.
     */
    uint32_t GetQidx() const;

    /**
     * \brief set the index of queueDis that the item should be put in.
     * \param qIdx the index of queueDis.
     */
    void SetQidx(uint32_t qIdx);

  private:
    uint32_t m_qIdx; //!< The index of queueDisc that the item shuld be put in.
};

} // namespace ns3

#endif /* PPFC_QUEUE_DISC_ITEM_H */
