#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/traffic-control-helper.h"

using namespace ns3;
bool verbose = true;

NS_LOG_COMPONENT_DEFINE ("NetBoost");

static bool firstCwnd = true;
static bool firstSshThr = true;
static bool firstRtt = true;
static bool firstRto = true;
static Ptr<OutputStreamWrapper> cWndStream;
static Ptr<OutputStreamWrapper> ssThreshStream;
static Ptr<OutputStreamWrapper> rttStream;
static Ptr<OutputStreamWrapper> rtoStream;
static Ptr<OutputStreamWrapper> nextTxStream;
static Ptr<OutputStreamWrapper> nextRxStream;
static Ptr<OutputStreamWrapper> inFlightStream;
static Ptr<OutputStreamWrapper> ackStream; 
static Ptr<OutputStreamWrapper> congStateStream; 
static Ptr<OutputStreamWrapper> tcpTxStream;
static Ptr<OutputStreamWrapper> tcpRxStream;
static uint32_t cWndValue;
static uint32_t ssThreshValue;

static Ptr<OutputStreamWrapper> packetSinkDelayStream;
static Ptr<OutputStreamWrapper> sojournTimeStream;
static Ptr<OutputStreamWrapper> ppbpTxStream;
static Ptr<OutputStreamWrapper> ppbpRxStream;

static void PacketSinkDelayTracer(Ptr<const Packet> p, const Address & from, const Address & to, const SeqTsSizeHeader& ts_header){
  *packetSinkDelayStream->GetStream()<<ts_header.GetSeq()<<" "<<ts_header.GetTs().GetSeconds()<<" "<<Simulator::Now ().GetSeconds ()<<std::endl;
}

static void SojournTimeTracer(Time sojourn_time){
  *sojournTimeStream->GetStream () <<Simulator::Now ().GetSeconds () << " " <<sojourn_time.ToDouble (Time::MS)<<std::endl;
}

static void PPBPTxTracer(Ptr<const Packet> new_p){
  *ppbpTxStream->GetStream()<<Simulator::Now ().GetSeconds () << " " <<new_p->GetSize()<<std::endl;
}

static void TracePPBPTx(std::string &ppbp_tx_file_name){
  AsciiTraceHelper ascii;
  ppbpTxStream = ascii.CreateFileStream(ppbp_tx_file_name.c_str());
  Config::ConnectWithoutContext("/NodeList/4/ApplicationList/0/Tx",MakeCallback(&PPBPTxTracer));
}

static void PPBPRxTracer(Ptr<const Packet> new_p,const Address & source){
  if (new_p != Ptr<const Packet>()){
      *ppbpRxStream->GetStream()<<Simulator::Now().GetSeconds() << " "<<new_p->GetSize()<<std::endl;
    }

}

static void TracePPBPRx(std::string &ppbp_rx_file_name){
  AsciiTraceHelper ascii;
  ppbpTxStream = ascii.CreateFileStream(ppbp_rx_file_name.c_str());
  Config::ConnectWithoutContext("/NodeList/5/ApplicationList/0/Rx",MakeCallback(&PPBPRxTracer));
}

static void
AckTracer (SequenceNumber32 old, SequenceNumber32 newAck)
{
  *ackStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newAck << std::endl;
}

static void
CongStateTracer (TcpSocketState::TcpCongState_t old, TcpSocketState::TcpCongState_t newState)
{
  *congStateStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newState << std::endl;
}

static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd)
    {
      *cWndStream->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd = false;
    }
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue = newval;

  if (!firstSshThr)
    {
      *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue << std::endl;
    }
}

static void
SsThreshTracer (uint32_t oldval, uint32_t newval)
{
  if (firstSshThr)
    {
      *ssThreshStream->GetStream () << "0.0 " << oldval << std::endl;
      firstSshThr = false;
    }
  *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  ssThreshValue = newval;

  if (!firstCwnd)
    {
      *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cWndValue << std::endl;
    }
}

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
RtoTracer (Time oldval, Time newval)
{
  if (firstRto)
    {
      *rtoStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRto = false;
    }
  *rtoStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
NextTxTracer (SequenceNumber32 old, SequenceNumber32 nextTx)
{
  NS_UNUSED (old);
  std::time_t now = time(0);
  std::tm* ltm = std::localtime(&now);
  *nextTxStream->GetStream () << ltm->tm_hour<<":"<<ltm->tm_min<<":"<<ltm->tm_sec<<" "
                              << Simulator::Now ().GetSeconds () << " " << nextTx << std::endl;
}

static void TcpTxTracer(Ptr<const Packet> p, const TcpHeader& th,
             Ptr<const TcpSocketBase> tcp_socket)
{
  std::time_t now = time(0);
  std::tm* ltm = std::localtime(&now);
  *tcpTxStream->GetStream()<< ltm->tm_hour<<":"<<ltm->tm_min<<":"<<ltm->tm_sec<<" "
                             <<Simulator::Now().GetSeconds () << " "<<th.GetSequenceNumber()<<std::endl;
}

static void TcpRxTracer(Ptr<const Packet> p, const TcpHeader& th,
             Ptr<const TcpSocketBase> tcp_socket)
{
  std::time_t now = time(0);
  std::tm* ltm = std::localtime(&now);
  *tcpRxStream->GetStream()<< ltm->tm_hour<<":"<<ltm->tm_min<<":"<<ltm->tm_sec<<" "
                             <<Simulator::Now().GetSeconds () << " "<<th.GetSequenceNumber()<<std::endl;
}

static void
InFlightTracer (uint32_t old, uint32_t inFlight)
{
  NS_UNUSED (old);
  *inFlightStream->GetStream () << Simulator::Now ().GetSeconds () << " " << inFlight << std::endl;
}

static void
NextRxTracer (SequenceNumber32 old, SequenceNumber32 nextRx)
{
  NS_UNUSED (old);
  *nextRxStream->GetStream () << Simulator::Now ().GetSeconds () << " " << nextRx << std::endl;
}

static void
TraceAck (std::string &ack_file_name)
{
  AsciiTraceHelper ascii;
  ackStream = ascii.CreateFileStream (ack_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0//HighestRxAck", MakeCallback (&AckTracer));
}

static void
TraceCongState (std::string &cong_state_file_name)
{
  AsciiTraceHelper ascii;
  congStateStream = ascii.CreateFileStream (cong_state_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongState", MakeCallback (&CongStateTracer));
}

static void
TraceCwndInflate (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));
}

static void
TraceSsThresh (std::string ssthresh_tr_file_name)
{
  AsciiTraceHelper ascii;
  ssThreshStream = ascii.CreateFileStream (ssthresh_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/SlowStartThreshold", MakeCallback (&SsThreshTracer));
}

static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}

static void
TraceRto (std::string rto_tr_file_name)
{
  AsciiTraceHelper ascii;
  rtoStream = ascii.CreateFileStream (rto_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTO", MakeCallback (&RtoTracer));
}

static void
TraceNextTx (std::string &next_tx_seq_file_name)
{
  AsciiTraceHelper ascii;
  nextTxStream = ascii.CreateFileStream (next_tx_seq_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/NextTxSequence", MakeCallback (&NextTxTracer));
}

static void
TraceInFlight (std::string &in_flight_file_name)
{
  AsciiTraceHelper ascii;
  inFlightStream = ascii.CreateFileStream (in_flight_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/BytesInFlight", MakeCallback (&InFlightTracer));
}


static void
TraceNextRx (std::string &next_rx_seq_file_name)
{
  AsciiTraceHelper ascii;
  nextRxStream = ascii.CreateFileStream (next_rx_seq_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/1/RxBuffer/NextRxSequence", MakeCallback (&NextRxTracer));
}

static void TraceTcpTx(std::string &tcp_tx_file_name)
{
  AsciiTraceHelper ascii;
  tcpTxStream = ascii.CreateFileStream(tcp_tx_file_name.c_str ());
  Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/Tx",MakeCallback(&TcpTxTracer));
}

static void TraceTcpRx(std::string &tcp_rx_file_name)
{
  AsciiTraceHelper ascii;
  tcpRxStream = ascii.CreateFileStream(tcp_rx_file_name.c_str ());
  Config::ConnectWithoutContext("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/Rx",MakeCallback(&TcpRxTracer));
}

int
main (int argc, char *argv[])
{
  bool onPPBP = true;
  bool tracing = true;

  std::string access_bandwidth = "100Mbps";
  std::string access_delay = "2ms";
  std::string bottle_bandwidth = "10Mbps";
  std::string bottle_delay = "20ms";

  std::string transport_prot = "TcpNewReno";
  bool sack = false;
  std::string prefix_file_name = "thesis/IPv6_";

  // PPBP traffic generator, e.g. average sending rate = 0.5 * 100 * 0.1 = 5Mpbs
  std::string ppbp_datarate="50Kbps"; // 0.5Mbps
  double ppbp_burst_arrival = 100.0; // 100 times/seconds
  double ppbp_burst_length = 0.2; // 0.1 second
  double ppbp_hurst = 0.8;
  bool ppbp_tcp = false;

  std::string queue_disc_type = "ns3::PfifoFastQueueDisc";
  double queue_size = 1;

  double duration = 65;
  uint32_t mtu_bytes = 1500;
  double error_p = 0.0;


  CommandLine cmd;
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, TcpLinuxReno, "
                                  "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                                  "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                                  "TcpLp, TcpDctcp, TcpCubic, TcpBbr", transport_prot);
  cmd.AddValue("onPPBP","Turn on or off the PPBP background traffic", onPPBP);
  cmd.AddValue("verbose","Output transmission and reception timestamps",verbose);
  cmd.AddValue("access_bandwidth","Access link bandwidth",access_bandwidth);
  cmd.AddValue("access_delay","Access link delay",access_delay);
  cmd.AddValue("bottle_bandwidth","Bottleneck link bandwidth",bottle_bandwidth);
  cmd.AddValue("bottle_delay","Bottleneck link delay",bottle_delay);
  cmd.AddValue("ppbp_datarate","PPBP sending rate, e.g. 0.5Mbps",ppbp_datarate);
  cmd.AddValue("ppbp_burst_arrival","PPBP burst arrival rate, the lambda",ppbp_burst_arrival);
  cmd.AddValue("ppbp_burst_length","PPBP burst arrival length",ppbp_burst_length);
  cmd.AddValue ("queue_disc_type", "Queue disc type for gateway (e.g. ns3::CoDelQueueDisc)", queue_disc_type);
  cmd.AddValue("ppbp_hurst","PPBP Hurst parameter",ppbp_hurst);
  cmd.AddValue ("sack", "Enable or disable SACK option", sack);
  cmd.AddValue("duration","The time to sending packets to network",duration);
  cmd.AddValue("tracing","Tracing all the tracing source to output files",tracing);
  cmd.AddValue ("prefix_name", "Prefix of output trace file", prefix_file_name);
  cmd.AddValue ("mtu", "Size of IP packets to send in bytes", mtu_bytes);
  cmd.AddValue("queue_size","Queue size of the queue, 1 present BDP*RTT", queue_size);
  cmd.Parse (argc, argv);

  //Time::SetResolution (Time::NS);

  LogComponentEnable ("NetBoost", LOG_LEVEL_WARN);

  // form output file name from parameters
  std::ostringstream buffer;
  buffer.setf (std::ios::fixed);
  buffer.precision(1);
  buffer<<prefix_file_name<<transport_prot<<"_"<<ppbp_hurst<<"_"<<ppbp_burst_arrival<<"_"<<ppbp_burst_length<<"_"<<ppbp_datarate;
  if(ppbp_tcp){
      buffer<<"_PPBPtcp";
    }
  prefix_file_name = buffer.str();
  NS_LOG_INFO ("Output Stream will be "<<prefix_file_name);

  AsciiTraceHelper ascii;
  sojournTimeStream = ascii.CreateFileStream ((prefix_file_name+"-sojourn.data").c_str());
  packetSinkDelayStream = ascii.CreateFileStream ((prefix_file_name+"-delay.data").c_str());

  uint32_t run = 0;

  SeedManager::SetSeed (1);
  SeedManager::SetRun (run);

  Header* temp_header = new Ipv4Header ();
  uint32_t ip_header = temp_header->GetSerializedSize ();
  NS_LOG_LOGIC ("IP Header size is: " << ip_header);
  delete temp_header;
  temp_header = new TcpHeader ();
  uint32_t tcp_header = temp_header->GetSerializedSize ();
  NS_LOG_LOGIC ("TCP Header size is: " << tcp_header);
  delete temp_header;
  uint32_t tcp_adu_size = mtu_bytes - 20 - (ip_header + tcp_header);
  NS_LOG_LOGIC ("TCP ADU size is: " << tcp_adu_size);

  temp_header = new UdpHeader ();
  uint32_t udp_header = temp_header->GetSerializedSize ();
  NS_LOG_LOGIC ("UDP Header size is: " << udp_header);
  delete temp_header;
  uint32_t udp_adu_size = mtu_bytes - 20 - (ip_header + tcp_header);
  NS_LOG_LOGIC ("UDP ADU size is: " << udp_adu_size);

  NS_LOG_INFO("Set TCP congestion control algorithm :"<<transport_prot);
  transport_prot = std::string ("ns3::") + transport_prot;
  if (transport_prot.compare ("ns3::TcpWestwoodPlus") == 0)
    {
      // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
      // the default protocol type in ns3::TcpWestwood is WESTWOOD
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
    }
  else
    {
      TypeId tcpTid;
      NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (transport_prot, &tcpTid), "TypeId " << transport_prot << " not found");
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot)));
    }

  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (sack));
  // set TCP buffer as 1Gb
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1 << 30));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1 << 30));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (tcp_adu_size));

  NS_LOG_LOGIC("Creating Topology");
  NodeContainer measureNodes;
  measureNodes.Create (2);
  NodeContainer routerNodes;
  routerNodes.Create(2);
  NodeContainer trafficNodes;
  trafficNodes.Create(2);

  PointToPointHelper p2p_access;
  p2p_access.SetDeviceAttribute ("DataRate", StringValue (access_bandwidth));
  p2p_access.SetChannelAttribute ("Delay", StringValue (access_delay));
  NetDeviceContainer device_mArA = p2p_access.Install(measureNodes.Get (0),routerNodes.Get(0));
  NetDeviceContainer device_mBrB = p2p_access.Install(measureNodes.Get(1),routerNodes.Get(1));
  NetDeviceContainer device_tArA = p2p_access.Install(trafficNodes.Get(0),routerNodes.Get(0));
  NetDeviceContainer device_tBrB = p2p_access.Install(trafficNodes.Get(1),routerNodes.Get(1));

  PointToPointHelper p2p_bottle;
  p2p_bottle.SetDeviceAttribute ("DataRate",StringValue (bottle_bandwidth));
  p2p_bottle.SetChannelAttribute("Delay",StringValue (bottle_delay));
  NetDeviceContainer device_rArB = p2p_bottle.Install(routerNodes);

  NS_LOG_LOGIC ("Install internet stack");
  InternetStackHelper stack_ipv4_only;
  stack_ipv4_only.SetIpv6StackInstall(false);
  stack_ipv4_only.Install (measureNodes);
  stack_ipv4_only.Install(trafficNodes);

  InternetStackHelper stack;
  stack.Install(routerNodes);

  DataRate access_b (access_bandwidth);
  DataRate bottle_b (bottle_bandwidth);
  Time access_d (access_delay);
  Time bottle_d (bottle_delay);

  uint32_t size = (bottle_b.GetBitRate () / 8) *
                  ((bottle_d.GetSeconds ()+access_d.GetSeconds())*2)*queue_size;
  //  uint32_t size = (bottle_b.GetBitRate () / 8) *
  //                  (bottle_d.GetSeconds ())*queue_size;

  NS_LOG_LOGIC("The buffer size of the bottle link is "<<size<<" bytes");

  NetDeviceContainer all_device;
  all_device.Add (device_mArA);
  all_device.Add (device_mBrB);
  all_device.Add (device_tArA);
  all_device.Add (device_tBrB);
  all_device.Add (device_rArB);

  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc", "MaxSize",
                             QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, int(size/mtu_bytes))));
  QueueDiscContainer qdiscs = tchPfifo.Install (all_device);
  Ptr<QueueDisc> q = qdiscs.Get (8);
  q->TraceConnectWithoutContext("SojournTime",MakeCallback (&SojournTimeTracer));


  NS_LOG_LOGIC ("Assgin addresses");
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if_mArA = address.Assign (device_mArA);
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if_mBrB = address.Assign(device_mBrB);
  address.SetBase("10.1.3.0","255.255.255.0");
  Ipv4InterfaceContainer if_tArA = address.Assign(device_tArA);
  address.SetBase("10.1.4.0","255.255.255.0");
  Ipv4InterfaceContainer if_tBrB = address.Assign(device_tBrB);

  Ipv6AddressHelper address_v6;
  address_v6.SetBase("2111:cfff:bbbb::",Ipv6Prefix(48));
  Ipv6InterfaceContainer if_rArB = address_v6.Assign(device_rArB);

  Ptr<Ipv6> rA_ipv6 =(routerNodes.Get(0)->GetObject<Ipv6>());
  Ptr<Ipv6> rB_ipv6 =(routerNodes.Get(1)->GetObject<Ipv6>());
  rA_ipv6->SetForwarding (1,true);
  rB_ipv6->SetForwarding (1,true);

  NS_LOG_LOGIC ("Routing");
  Ipv4StaticRoutingHelper ipv4_route_helper;
  Ptr<Ipv4StaticRouting> static_routing_mA = ipv4_route_helper.GetStaticRouting (measureNodes.Get(0)->GetObject<Ipv4>());
  static_routing_mA->SetDefaultRoute(if_mArA.GetAddress(1),1);
  Ptr<Ipv4StaticRouting> static_routing_mB = ipv4_route_helper.GetStaticRouting (measureNodes.Get(1)->GetObject<Ipv4>());
  static_routing_mB->SetDefaultRoute(if_mBrB.GetAddress(1),1);
  Ptr<Ipv4StaticRouting> static_routing_tA = ipv4_route_helper.GetStaticRouting (trafficNodes.Get(0)->GetObject<Ipv4>());
  static_routing_tA->SetDefaultRoute(if_tArA.GetAddress(1),1);
  Ptr<Ipv4StaticRouting> static_routing_tB = ipv4_route_helper.GetStaticRouting (trafficNodes.Get(1)->GetObject<Ipv4>());
  static_routing_tB->SetDefaultRoute(if_tBrB.GetAddress(1),1);

  Ptr<Ipv4StaticRouting> static_routing_rA = ipv4_route_helper.GetStaticRouting (routerNodes.Get(0)->GetObject<Ipv4>());
  static_routing_rA->AddNetworkRouteTo (Ipv4Address("10.1.2.0"),Ipv4Mask("255.255.255.0"),1,Ipv6Address("2111:cafe:eeee::"),Ipv6Address("2111:cafe:eeee::"),
                                        Ipv6Prefix(48),Ipv6Prefix(48));
  static_routing_rA->AddNetworkRouteTo (Ipv4Address("10.1.4.0"),Ipv4Mask("255.255.255.0"),1,Ipv6Address("2111:cafe:eeee::"),Ipv6Address("2111:cafe:eeee::"),
                                        Ipv6Prefix(48),Ipv6Prefix(48));
  Ptr<Ipv4StaticRouting> static_routing_rB = ipv4_route_helper.GetStaticRouting (routerNodes.Get(1)->GetObject<Ipv4>());
  static_routing_rB->AddNetworkRouteTo (Ipv4Address("10.1.1.0"),Ipv4Mask("255.255.255.0"),1,Ipv6Address("2111:cafe:eeee::"),Ipv6Address("2111:cafe:eeee::"),
                                        Ipv6Prefix(48),Ipv6Prefix(48));
  static_routing_rB->AddNetworkRouteTo (Ipv4Address("10.1.3.0"),Ipv4Mask("255.255.255.0"),1,Ipv6Address("2111:cafe:eeee::"),Ipv6Address("2111:cafe:eeee::"),
                                        Ipv6Prefix(48),Ipv6Prefix(48));

  Ipv6StaticRoutingHelper ipv6_route_helper;
  Ptr<Ipv6StaticRouting> static_routing_rA_v6 = ipv6_route_helper.GetStaticRouting(rA_ipv6);
  static_routing_rA_v6->AddIVINetworkRouteTo (Ipv6Address("2111:cafe:eeee:0a01:0100::"),Ipv6Prefix(72),Ipv6Prefix(48),1,0,true);
  static_routing_rA_v6->AddIVINetworkRouteTo (Ipv6Address("2111:cafe:eeee:0a01:0300::"),Ipv6Prefix(72),Ipv6Prefix(48),2,0,true);
  Ptr<Ipv6StaticRouting> static_routing_rB_v6 = ipv6_route_helper.GetStaticRouting(rB_ipv6);
  static_routing_rB_v6->AddIVINetworkRouteTo (Ipv6Address("2111:cafe:eeee:0a01:0200::"),Ipv6Prefix(72),Ipv6Prefix(48),1,0,true);
  static_routing_rB_v6->AddIVINetworkRouteTo (Ipv6Address("2111:cafe:eeee:0a01:0400::"),Ipv6Prefix(72),Ipv6Prefix(48),2,0,true);

  double start_time = 0;
  double stop_time = start_time + duration;

  double tcp_start_time = 0;

  NS_LOG_LOGIC ("Build TCP sender and receiver on measurement ndoes");
  uint16_t measure_port = 6001;
  BulkSendHelper ftp("ns3::TcpSocketFactory",InetSocketAddress(if_mBrB.GetAddress(0),measure_port));
  uint32_t maxBytes = 0; // the total number of data sending, 0 means no limit
  ftp.SetAttribute ("SendSize", UintegerValue (tcp_adu_size));
  ftp.SetAttribute("MaxBytes",UintegerValue(maxBytes));
  ftp.SetAttribute ("EnableSeqTsSizeHeader",BooleanValue (true));

  ApplicationContainer sourceApp = ftp.Install(measureNodes.Get(0));
  sourceApp.Start(Seconds(tcp_start_time));
  sourceApp.Stop(Seconds(stop_time));

  PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), measure_port));
  sinkHelper.SetAttribute ("EnableSeqTsSizeHeader",BooleanValue (true));
  sinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
  ApplicationContainer sinkApp = sinkHelper.Install(measureNodes.Get(1));
  sinkApp.Get(0)->TraceConnectWithoutContext("RxWithSeqTsSize",MakeCallback(&PacketSinkDelayTracer));

  sinkApp.Start(Seconds(tcp_start_time));
  sinkApp.Stop(Seconds(stop_time));


  if (tracing)
    {
      NS_LOG_LOGIC("Configure Tracing");

      double tcp_tracing_start = tcp_start_time+0.00001;
      double tcp_tracing_start_more = tcp_start_time+0.2;
      double ppbp_tracing_start = 0.00001;
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceSsThresh, prefix_file_name + "-ssth.data");
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceRtt, prefix_file_name + "-rtt.data");
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceRto, prefix_file_name + "-rto.data");
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceNextTx, prefix_file_name + "-next-tx.data");
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceInFlight, prefix_file_name + "-inflight.data");
      Simulator::Schedule (Seconds (tcp_tracing_start_more), &TraceNextRx, prefix_file_name + "-next-rx.data");
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceAck, prefix_file_name + "-ack.data"); 
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceCongState, prefix_file_name + "-cong-state.data");
      Simulator::Schedule (Seconds (tcp_tracing_start), &TraceCwndInflate, prefix_file_name + "-cwnd.data");
    }


  if (onPPBP){
      NS_LOG_LOGIC ("Build PPBP data sender and receiver as background traffic");
      std::string ppbp_socket = "ns3::UdpSocketFactory";
      if(ppbp_tcp){
          ppbp_socket = "ns3::TcpSocketFactory";
        }

      uint16_t ppbp_port = 9;
      PPBPHelper ppbp = PPBPHelper(ppbp_socket,
                                    InetSocketAddress(if_tBrB.GetAddress(0), ppbp_port));

      ppbp.SetAttribute("BurstIntensity", DataRateValue(DataRate(ppbp_datarate))); //
      ppbp.SetAttribute("PacketSize", UintegerValue(udp_adu_size));
      ppbp.SetAttribute("H",DoubleValue(ppbp_hurst));

      std::ostringstream buffer;
      buffer.setf (std::ios::fixed);
      buffer.precision(1);
      buffer<<"ns3::ConstantRandomVariable[Constant="<<ppbp_burst_arrival<<"]";
      ppbp.SetAttribute("MeanBurstArrivals", StringValue(buffer.str()));
      buffer.str("");
      buffer<<"ns3::ConstantRandomVariable[Constant="<<ppbp_burst_length<<"]";
      ppbp.SetAttribute("MeanBurstTimeLength", StringValue(buffer.str()));

      ApplicationContainer ppbpApps = ppbp.Install(trafficNodes.Get(0));


      PacketSinkHelper ppbp_sink(ppbp_socket,
                                  InetSocketAddress(Ipv4Address::GetAny(), ppbp_port));
      ApplicationContainer ppbpSink = ppbp_sink.Install(trafficNodes.Get(1));
      ppbpApps.Start(Seconds(0.0));
      ppbpApps.Stop(Seconds(duration));
      ppbpSink.Start(Seconds(0.0));
      ppbpSink.Stop(Seconds(duration));

      Simulator::Schedule (Seconds (0.00001), &TracePPBPTx, prefix_file_name + "-ppbptx.data");

      std::time_t start_time = time(0);

      NS_LOG_INFO("Start Simulator");
      Simulator::Stop(Seconds(duration));
      Simulator::Run ();

      std::time_t end_time = time(0);

      float time_elapsed = end_time - start_time;
      NS_LOG_INFO ("The time elapsed is "<<time_elapsed<<" seconds");


      Ptr<PacketSink> ppbpSink_ptr = DynamicCast<PacketSink> (ppbpSink.Get(0));
      double ppbp_goodput = 8*(ppbpSink_ptr->GetTotalRx()/duration)/1000000;
      NS_LOG_INFO ("The goodput of PPBP application is "<<ppbp_goodput<<" Mbps");

      Ptr<PacketSink> measure_ptr = DynamicCast<PacketSink> (sinkApp.Get(0));
      double measure_goodput = 8*(measure_ptr->GetTotalRx()/(duration-tcp_start_time))/1000000;
      NS_LOG_WARN ("The goodput of bulk TCP sender application is "<<measure_goodput<<" Mbps");
    }
  else{
      NS_LOG_INFO("Start Simulator, stop at "<<stop_time);
      Simulator::Stop(Seconds(stop_time));
      Simulator::Run ();
      Ptr<PacketSink> measure_ptr = DynamicCast<PacketSink> (sinkApp.Get(0));
      double measure_goodput = 8*(measure_ptr->GetTotalRx())/(duration*1000);
      NS_LOG_WARN ("The goodput of bulk TCP sender application is "<<measure_goodput<<" Kbps");

    }

  Simulator::Destroy ();
  return 0;
}
