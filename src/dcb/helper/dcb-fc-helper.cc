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

#include "dcb-fc-helper.h"

#include "ns3/arp-l3-protocol.h"
#include "ns3/assert.h"
#include "ns3/dcb-channel.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/ipv4.h"
#include "ns3/string.h"

#include <vector>

namespace ns3
{

DcbFcHelper::DcbFcHelper()
    : m_bufferSize(QueueSize("0B")),
      m_bufferPerPort(QueueSize("0B")),
      m_bufferBandwidthRatio(0.0),
      m_numQueuePerPort(8),
      m_numLosslessQueue(2),
      m_maxCredit(0)
{
}

DcbFcHelper::~DcbFcHelper()
{
}

// static
void
DcbFcHelper::InstallPFCtoNodePort(Ptr<Node> node,
                                  const uint32_t port,
                                  const DcbPfcPortConfig& config)
{
    Ptr<DcbTrafficControl> dcbTc = node->GetObject<DcbTrafficControl>();
    NS_ASSERT_MSG(dcbTc, "PFC enabled but there is no DcbTrafficControl aggregated to the node");

    Ptr<NetDevice> dev = node->GetDevice(port);

    // enable flow control on queue disc
    Ptr<PausableQueueDisc> qDisc = DynamicCast<DcbNetDevice>(node->GetDevice(port))->GetQueueDisc();
    qDisc->SetFCEnabled(true);

    // install PFC
    Ptr<DcbPfcPort> pfc = CreateObject<DcbPfcPort>(dev, dcbTc);
    uint8_t enableVec = 0;
    std::vector<Ptr<DcbFlowControlMmuQueue>> mmuQueues;
    for (const DcbPfcPortConfig::QueueConfig& qConfig : config.queues)
    {
        if (qConfig.priority >= DcbTrafficControl::PRIORITY_NUMBER)
        {
            NS_FATAL_ERROR("PFC priority should be 0~" << DcbTrafficControl::PRIORITY_NUMBER - 1
                                                       << ", your input is " << qConfig.priority);
        }
        if (qConfig.resumeOffset > qConfig.reserve)
        {
            NS_FATAL_ERROR("resumeOffset should be less or equal to reserve");
        }
        enableVec |= (1 << qConfig.priority);
        // pfc->ConfigQueue(qConfig.priority, qConfig.reserve, qConfig.xon);
        Ptr<DcbPfcMmuQueue> mmuQueue = CreateObject<DcbPfcMmuQueue>(qConfig.reserve,
                                                                    qConfig.resumeOffset,
                                                                    qConfig.headroom,
                                                                    qConfig.isDynamicThreshold,
                                                                    qConfig.dtShift);
        mmuQueues.push_back(mmuQueue);
    }
    pfc->SetEnableVec(enableVec);
    dcbTc->InstallFCToPort(port, pfc, mmuQueues);

    // register protocol handler
    node->RegisterProtocolHandler(MakeCallback(&DcbPfcPort::ReceivePfc, pfc),
                                  PfcFrame::PROT_NUMBER,
                                  dev);
}

// static
void
DcbFcHelper::InstallPFCtoHostPort(Ptr<Node> node, const uint32_t port, const uint8_t enableVec)
{
    Ptr<NetDevice> dev = node->GetDevice(port);

    // enable flow control on queue disc
    Ptr<PausableQueueDisc> qDisc = DynamicCast<DcbNetDevice>(node->GetDevice(port))->GetQueueDisc();
    qDisc->SetFCEnabled(true);

    // install PFC, pass a null DcbTrafficControl to DcbPfcPort
    Ptr<DcbPfcPort> pfc = CreateObject<DcbPfcPort>(dev, Ptr<DcbTrafficControl>(0));
    pfc->SetEnableVec(enableVec);

    // register protocol handler
    node->RegisterProtocolHandler(MakeCallback(&DcbPfcPort::ReceivePfc, pfc),
                                  PfcFrame::PROT_NUMBER,
                                  dev);
}

void
DcbFcHelper::InstallHpccPFCtoNodePort(Ptr<Node> node,
                                      const uint32_t port,
                                      const DcbPfcPortConfig& config)
{
    Ptr<DcbTrafficControl> dcbTc = node->GetObject<DcbTrafficControl>();
    NS_ASSERT_MSG(dcbTc, "PFC enabled but there is no DcbTrafficControl aggregated to the node");

    Ptr<NetDevice> dev = node->GetDevice(port);

    // enable flow control on queue disc
    Ptr<PausableQueueDisc> qDisc = DynamicCast<DcbNetDevice>(node->GetDevice(port))->GetQueueDisc();
    qDisc->SetFCEnabled(true);

    // install HpccPfc
    Ptr<DcbHpccPort> hpccPfc = CreateObject<DcbHpccPort>(dev, dcbTc);
    uint8_t enableVec = 0;
    std::vector<Ptr<DcbFlowControlMmuQueue>> mmuQueues;
    for (const DcbPfcPortConfig::QueueConfig& qConfig : config.queues)
    {
        if (qConfig.priority >= DcbTrafficControl::PRIORITY_NUMBER)
        {
            NS_FATAL_ERROR("PFC priority should be 0~" << DcbTrafficControl::PRIORITY_NUMBER - 1
                                                       << ", your input is " << qConfig.priority);
        }
        if (qConfig.resumeOffset > qConfig.reserve)
        {
            NS_FATAL_ERROR("resumeOffset should be less or equal to reserve");
        }
        enableVec |= (1 << qConfig.priority);
        // pfc->ConfigQueue(qConfig.priority, qConfig.reserve, qConfig.xon);
        Ptr<DcbPfcMmuQueue> mmuQueue = CreateObject<DcbPfcMmuQueue>(qConfig.reserve,
                                                                    qConfig.resumeOffset,
                                                                    qConfig.headroom,
                                                                    qConfig.isDynamicThreshold,
                                                                    qConfig.dtShift);
        mmuQueues.push_back(mmuQueue);
    }
    hpccPfc->SetEnableVec(enableVec);
    hpccPfc->SetFcEgressEnabled(true);
    dcbTc->InstallFCToPort(port, hpccPfc, mmuQueues);

    // register protocol handler
    node->RegisterProtocolHandler(MakeCallback(&DcbPfcPort::ReceivePfc, hpccPfc),
                                  PfcFrame::PROT_NUMBER,
                                  dev);
}

void
DcbFcHelper::InstallPPfctoSwitch(Ptr<Node> node,
                                 const uint32_t port,
                                 const DcbPPfcPortConfig& config,
                                 const uint32_t qNum)
{
    Ptr<DcbPPfcTrafficControl> dcbTc = node->GetObject<DcbPPfcTrafficControl>();
    NS_ASSERT_MSG(dcbTc, "PPFC enabled but there is no DcbTrafficControl aggregated to the node");

    Ptr<NetDevice> dev = node->GetDevice(port);

    // enable flow control on queue disc
    Ptr<PausableQueueDisc> qDisc = DynamicCast<DcbNetDevice>(node->GetDevice(port))->GetQueueDisc();
    qDisc->SetFCEnabled(true);
    DynamicCast<PPfcPausableQueueDisc>(qDisc)->SetPortQNum(qNum);

    // install ppfc-port
    Ptr<DcbPPfcPort> ppfcPort = CreateObject<DcbPPfcPort>(dev, dcbTc);
    std::vector<Ptr<DcbFlowControlMmuQueue>> mmuQueues;
    ObjectFactory factory;
    factory.SetTypeId("ns3::FifoQueueDiscEcn");
    for (uint32_t i = 0; i < qNum; i++)
    {
        Ptr<DcbPPfcMmuQueue> mmuQueue = CreateObject<DcbPPfcMmuQueue>(config.qReservedSize);
        mmuQueues.push_back(mmuQueue);

        // Ptr<QueueDisc> qd = factory.Create<QueueDisc>();
        // qd->Initialize();
        // Ptr<PPfcPausableQueueDiscClass> c = CreateObject<PPfcPausableQueueDiscClass>();
        // c->SetQueueDisc(qd);
        // qDisc->AddQueueDiscClass(c);
    }
    // config threshold in ppfc-port.
    for (auto& th : config.categories)
    {
        ppfcPort->AddCategory(th.xoff, th.xon);
    }
    dcbTc->InstallFCToPort(port, ppfcPort, mmuQueues);
    // register protocol handler
    node->RegisterProtocolHandler(MakeCallback(&DcbPPfcPort::ReceivePfc, ppfcPort),
                                  PPfcFrame::PROT_NUMBER,
                                  dev);
}

void
DcbFcHelper::InstallPPfctoHost(Ptr<Node> node, const uint32_t port, const uint32_t qNum)
{
    Ptr<DcbPPfcTrafficControl> dcbTc = node->GetObject<DcbPPfcTrafficControl>();
    NS_ASSERT_MSG(dcbTc, "PPFC enabled but there is no DcbTrafficControl aggregated to the node");

    Ptr<NetDevice> dev = node->GetDevice(port);

    // enable flow control on queue disc
    Ptr<PausableQueueDisc> qDisc = DynamicCast<DcbNetDevice>(node->GetDevice(port))->GetQueueDisc();
    qDisc->SetFCEnabled(true);
    DynamicCast<PPfcPausableQueueDisc>(qDisc)->SetPortQNum(qNum);

    // install ppfc-port
    Ptr<DcbPPfcPort> ppfcPort = CreateObject<DcbPPfcPort>(dev, dcbTc);
    std::vector<Ptr<DcbFlowControlMmuQueue>> mmuQueues;
    ObjectFactory factory;
    factory.SetTypeId("ns3::FifoQueueDiscEcn");
    for (uint32_t i = 0; i < qNum; i++)
    {
        Ptr<DcbPPfcMmuQueue> mmuQueue =
            CreateObject<DcbPPfcMmuQueue>(QueueSize(QueueSizeUnit::BYTES, 1e4).GetValue());
        mmuQueues.push_back(mmuQueue);

        // Ptr<QueueDisc> qd = factory.Create<QueueDisc>();
        // qd->Initialize();
        // Ptr<PPfcPausableQueueDiscClass> c = CreateObject<PPfcPausableQueueDiscClass>();
        // c->SetQueueDisc(qd);
        // qDisc->AddQueueDiscClass(c);
    }
    dcbTc->InstallFCToPort(port, ppfcPort, mmuQueues);
    // register protocol handler
    node->RegisterProtocolHandler(MakeCallback(&DcbPPfcPort::ReceivePfc, ppfcPort),
                                  PPfcFrame::PROT_NUMBER,
                                  dev);
}

static void
AssignAddress(const Ptr<Node> node, const Ptr<NetDevice> device)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ASSERT_MSG(ipv4,
                  "Ipv4AddressHelper::Assign(): NetDevice is associated"
                  " with a node without IPv4 stack installed -> fail "
                  "(maybe need to use DcbStackHelper?)");

    int32_t interface = ipv4->GetInterfaceForDevice(device);
    if (interface == -1)
    {
        interface = ipv4->AddInterface(device);
    }
    NS_ASSERT_MSG(interface >= 0,
                  "Ipv4AddressHelper::Assign(): "
                  "Interface index not found");

    Ipv4Address addr = Ipv4AddressGenerator::NextAddress("255.0.0.0");
    Ipv4InterfaceAddress ipv4Addr = Ipv4InterfaceAddress(addr, "255.0.0.0");
    ipv4->AddAddress(interface, ipv4Addr);
    ipv4->SetMetric(interface, 1);
    ipv4->SetUp(interface);
}

void
DcbFcHelper::Install(Ptr<Node> node)
{
    /******* INSTALL TrafficControlLayer *******/
    node->AggregateObject(m_tcFactory.Create<Object>());

    const uint32_t devN = node->GetNDevices();
    Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
    // Hereafter, dcbTc == nullptr serve as a flag if fc is disabled
    Ptr<DcbTrafficControl> dcbTc = node->GetObject<DcbTrafficControl>();

    // Check the buffer settings, first bufferBandwidthRatio, then bufferPerPort, then bufferSize
    if (m_bufferBandwidthRatio != 0.0)
    {
        // Calculate the buffer size according to the bandwidth and the bufferBandwidthRatio
        // (MB/Tbps)
        double totalBandwidth = 0.0; // in Tbps
        for (uint32_t i = 1; i < devN; i++)
        {
            Ptr<DcbNetDevice> nodeDev = DynamicCast<DcbNetDevice>(node->GetDevice(i));
            Ptr<DcbChannel> channel = DynamicCast<DcbChannel>(nodeDev->GetChannel());
            totalBandwidth += nodeDev->GetDataRate().GetBitRate() / 1e12; // in Tbps
        }
        m_bufferSize =
            QueueSize(QueueSizeUnit::BYTES, m_bufferBandwidthRatio * totalBandwidth * 1e6);
        std::cout << "Total Bandwidth: " << totalBandwidth
                  << "Tbps, Buffer size: " << m_bufferSize.GetValue() / 1e6 << "MB" << std::endl;
    }
    else if (m_bufferPerPort != QueueSize("0B"))
    {
        // If m_bufferPerPort is set, calculate the buffer size according to the number of ports
        m_bufferSize = QueueSize(m_bufferPerPort.GetUnit(),
                                 m_bufferPerPort.GetValue() * (node->GetNDevices() - 1));
    }
    else
    {
        // Do not use assert to ensure the correctness when running experiments
        NS_ABORT_MSG_IF(
            m_bufferSize == QueueSize("0B") && dcbTc != nullptr,
            "Buffer size is not set when fc is used, please set buffer size or buffer per port");
    }

    /******* INSTALL FlowControlPorts and FlowControlMmuQueues *******/
    if (dcbTc != nullptr)
    {
        dcbTc->RegisterDeviceNumber(devN); // to initialize the vector of ports info
    }
    // Install flow control port and mmu queue for each device
    for (uint32_t i = 1; i < devN; i++)
    {
        Ptr<DcbFlowControlPort> fcp = m_fcpFactory.Create<DcbFlowControlPort>();
        fcp->SetDevice(node->GetDevice(i));
        fcp->SetDcbTrafficControl(dcbTc);
        // register protocol handler
        // TODO this need to be refactor to support multiple protocol
        Ptr<DcbPfcPort> pfc = DynamicCast<DcbPfcPort>(fcp);
        if (pfc != nullptr)
        {
            node->RegisterProtocolHandler(MakeCallback(&DcbPfcPort::ReceivePfc, pfc),
                                          PfcFrame::PROT_NUMBER,
                                          node->GetDevice(i));
        }
        // TODO Not support other flow control protocol now

        if (m_fcmqFactory.GetTypeId() != TypeId())
        {
            std::vector<Ptr<DcbFlowControlMmuQueue>> mmuQueues;

            // Calculate the headroom for the queue
            uint32_t headroom = 0;
            // Get the channel associated with the device
            Ptr<DcbNetDevice> nodeDev = DynamicCast<DcbNetDevice>(node->GetDevice(i));
            Ptr<DcbChannel> channel = DynamicCast<DcbChannel>(nodeDev->GetChannel());
            // Headroom is calculated as 1.5 * channel rate * channel delay * 2
            headroom = 3. * nodeDev->GetDataRate().GetBitRate() * channel->GetDelay().GetSeconds() /
                       8.; // in bytes

            for (uint32_t j = 0; j < m_numQueuePerPort; j++)
            {
                Ptr<DcbFlowControlMmuQueue> fcmq = m_fcmqFactory.Create<DcbFlowControlMmuQueue>();
                mmuQueues.push_back(fcmq);
                fcmq->SetAttribute("HeadroomSize",
                                   QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, headroom)));
            }
            if (dcbTc != nullptr)
                dcbTc->InstallFCToPort(i, fcp, mmuQueues);

            // Compensate the used headroom for lossy queues to the buffer size
            // TODO different loss config for different queues
            m_bufferSize = QueueSize(QueueSizeUnit::BYTES,
                                     m_bufferSize.GetValue() +
                                         headroom * (m_numQueuePerPort - m_numLosslessQueue));
        }
    }

    // Set the buffer size of the whole node
    if (dcbTc != nullptr)
        dcbTc->SetAttribute("BufferSize", UintegerValue(m_bufferSize.GetValue()));

    /******* INSTALL Outer-QueueDiscs amd Inner-QueueDiscs *******/
    /*
     * In Pavin's pervious implementation, the last device is LoopbackDevice.
     * To respect the convention of ns-3, I move the loopback device to the first one.
     * I achive this by split the proto install process into two parts,
     * and install the loopback device by call switch protos install before install other ports.
     */
    // Install qdisc on all devices
    for (uint32_t i = 1; i < devN; i++)
    {
        Ptr<NetDevice> dev = node->GetDevice(i);
        // Here we assume all outer queue disc is PausableQueueDisc in fcHelper
        // Ptr<PausableQueueDisc> qDisc = CreateObject<PausableQueueDisc>(node, i);
        Ptr<PausableQueueDisc> qDisc = DynamicCast<PausableQueueDisc>(m_oqdFactory.Create());
        qDisc->SetNode(node);
        qDisc->SetPortIndex(i);
        tc->SetRootQueueDiscOnDevice(dev, qDisc);
        if (dcbTc == nullptr)
        {
            Ptr<DcbNetDevice> dcbDev = DynamicCast<DcbNetDevice>(dev);
            dcbDev->SetQueueDisc(qDisc);
        }

        // Install inner queue disc
        for (uint32_t j = 0; j < m_numQueuePerPort; j++)
        {
            // Set the size of the inner queue to the buffer size of the whole node when fc is
            // enabled
            if (dcbTc != nullptr)
                m_iqdFactory.Set("MaxSize", QueueSizeValue(m_bufferSize));
            Ptr<FifoQueueDiscEcn> qd = m_iqdFactory.Create<FifoQueueDiscEcn>();
            qd->Initialize();
            Ptr<PausableQueueDiscClass> c = CreateObject<PausableQueueDiscClass>();
            c->SetQueueDisc(qd);
            qDisc->AddQueueDiscClass(c);
        }

        // Set priorities and quantum, if have
        if (m_priorities.size() > 0)
        {
            NS_ASSERT_MSG(
                m_quantum.size() == m_priorities.size() && m_quantum.size() == m_numQueuePerPort,
                "The number of priorities and quantum should be the same as the number of "
                "inner queues");
            NS_ASSERT_MSG(m_maxCredit > 0, "The max credit should be set when WDRR is used");
            qDisc->SetWdrrParameters(m_priorities, m_quantum, m_maxCredit);
        }
        else
        {
            qDisc->SetDefaultStrictPriority();
        }
    }

    if (dcbTc != nullptr)
    {
        // If the traffic control layer is DcbTrafficControl, configure qdisc and dev to support fc
        PausableQueueDisc::TCEgressCallback tcCallback =
            MakeCallback(&DcbTrafficControl::EgressProcess, dcbTc);

        for (uint32_t i = 1; i < devN; i++)
        {
            // Get the queue disc of the device
            Ptr<NetDevice> dev = node->GetDevice(i);
            Ptr<PausableQueueDisc> qDisc = DynamicCast<DcbNetDevice>(dev)->GetQueueDisc();
            NS_ABORT_MSG_IF(qDisc == nullptr,
                            "QueueDisc is not installed or is not a PausableQueueDisc when TC is "
                            "DcbTrafficControl");
            qDisc->RegisterTrafficControlCallback(tcCallback);
        }
    }
    for (uint32_t i = 1; i < devN; i++)
    {
        Ptr<NetDevice> dev = node->GetDevice(i);
        Ptr<DcbNetDevice> dcbDev = DynamicCast<DcbNetDevice>(dev);
        NS_ABORT_MSG_IF(dcbDev == nullptr, "DcbNetDevice is not installed");
        dcbDev->SetFcEnabled(true); // all NetDevices should support FC;
    }

    // Assign address to all devices
    // Deferred to here as this function need trafficControlLayer to be installed
    for (uint32_t i = 1; i < devN; i++)
    {
        AssignAddress(node, node->GetDevice(i));
    }
    // Deferred to here as this function need trafficControlLayer to be installed
    Ptr<ArpL3Protocol> arp = node->GetObject<ArpL3Protocol>();
    arp->SetTrafficControl(tc);
}

void
DcbFcHelper::SetTrafficControlTypeId(const std::string& typeId)
{
    m_tcFactory.SetTypeId(typeId);
}

void
DcbFcHelper::SetFlowControlPortTypeId(const std::string& typeId)
{
    m_fcpFactory.SetTypeId(typeId);
}

void
DcbFcHelper::SetFlowControlMmuQueueTypeId(const std::string& typeId)
{
    m_fcmqFactory.SetTypeId(typeId);
}

void
DcbFcHelper::SetOuterQueueDiscTypeId(const std::string& typeId)
{
    m_oqdFactory.SetTypeId(typeId);
}

void
DcbFcHelper::SetInnerQueueDiscTypeId(const std::string& typeId)
{
    m_iqdFactory.SetTypeId(typeId);
}

void
DcbFcHelper::SetTrafficControlAttributes(std::vector<ConfigEntry_t>&& tcAttributes)
{
    for (auto& attr : tcAttributes)
    {
        m_tcFactory.Set(attr.first, *attr.second);

        // Get the buffer size from the attributes
        if (attr.first == "BufferSize")
        {
            Ptr<StringValue> qsv = DynamicCast<StringValue>(attr.second);
            m_bufferSize = QueueSize(qsv->Get());
            if (m_bufferSize.GetUnit() != QueueSizeUnit::BYTES)
            {
                NS_FATAL_ERROR("Buffer size should be in bytes");
            }
        }
    }
}

void
DcbFcHelper::SetFlowControlPortAttributes(std::vector<ConfigEntry_t>&& fcpAttributes)
{
    for (auto& attr : fcpAttributes)
    {
        m_fcpFactory.Set(attr.first, *attr.second);
    }
}

void
DcbFcHelper::SetFlowControlMmuQueueAttributes(std::vector<ConfigEntry_t>&& fcmqAttributes)
{
    for (auto& attr : fcmqAttributes)
    {
        m_fcmqFactory.Set(attr.first, *attr.second);
    }
}

void
DcbFcHelper::SetOuterQueueDiscAttributes(std::vector<ConfigEntry_t>&& oqdAttributes)
{
    for (auto& attr : oqdAttributes)
    {
        m_oqdFactory.Set(attr.first, *attr.second);
    }
}

void
DcbFcHelper::SetInnerQueueDiscAttributes(std::vector<ConfigEntry_t>&& iqdAttributes)
{
    for (auto& attr : iqdAttributes)
    {
        m_iqdFactory.Set(attr.first, *attr.second);
    }
}

void
DcbFcHelper::SetNumQueuePerPort(uint32_t numQueuePerPort)
{
    m_numQueuePerPort = numQueuePerPort;
}

void
DcbFcHelper::SetBufferBandwidthRatio(double bufferBandwidthRatio)
{
    m_bufferBandwidthRatio = bufferBandwidthRatio;
}

void
DcbFcHelper::SetNumLosslessQueue(uint32_t numLosslessQueue)
{
    m_numLosslessQueue = numLosslessQueue;
}

void
DcbFcHelper::SetBufferPerPort(QueueSize bufferPerPort)
{
    m_bufferPerPort = bufferPerPort;
    if (m_bufferPerPort.GetUnit() != QueueSizeUnit::BYTES)
    {
        NS_FATAL_ERROR("Buffer size should be in bytes");
    }
}

void
DcbFcHelper::SetBufferSize(QueueSize bufferSize)
{
    m_bufferSize = bufferSize;
    if (m_bufferSize.GetUnit() != QueueSizeUnit::BYTES)
    {
        NS_FATAL_ERROR("Buffer size should be in bytes");
    }
}

uint32_t
DcbFcHelper::GetNumQueuePerPort() const
{
    return m_numQueuePerPort;
}

void
DcbFcHelper::SetPriorities(std::vector<uint32_t>&& priorities)
{
    m_priorities = priorities;
}

void
DcbFcHelper::SetQuantum(std::vector<uint32_t>&& quantum)
{
    m_quantum = quantum;
}

void
DcbFcHelper::SetMaxCredit(uint32_t maxCredit)
{
    m_maxCredit = maxCredit;
}

} // namespace ns3
