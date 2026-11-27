using System;
using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;

namespace OurOS
{
    public sealed class DhcpFixture
    {
        static readonly IPAddress Server = IPAddress.Parse("192.0.2.1");
        static readonly IPAddress Client = IPAddress.Parse("192.0.2.2");
        static readonly byte[] ClientMac = { 2, 0, 0, 0, 0x18, 2 };
        const uint ServerWord = 0xc0000201, ClientWord = 0xc0000202;
        const uint Cookie = 0x63825363, LeaseSeconds = 80, T1 = 25, T2 = 55;
        const int Options = 240;
        readonly string mode;
        uint offered;
        long expires;

        DhcpFixture(string mode) { this.mode = mode; }

        static uint Read32(byte[] packet, int at)
        {
            return (uint)packet[at] << 24 | (uint)packet[at + 1] << 16 |
                   (uint)packet[at + 2] << 8 | packet[at + 3];
        }

        static void Write32(byte[] packet, int at, uint value)
        {
            for (int i = 3; i >= 0; --i) { packet[at + i] = (byte)value; value >>= 8; }
        }

        static bool HasClientMac(byte[] packet, int at)
        {
            for (int i = 0; i < ClientMac.Length; ++i)
                if (ClientMac[i] != packet[at + i]) return false;
            return true;
        }

        static void Option(byte[] packet, ref int at, byte code, uint value)
        {
            packet[at++] = code;
            packet[at++] = 4;
            Write32(packet, at, value);
            at += 4;
        }

        static byte[] Reply(byte type, uint xid, bool broadcast)
        {
            byte[] packet = new byte[300];
            packet[0] = 2;
            packet[1] = 1;
            packet[2] = 6;
            Write32(packet, 4, xid);
            if (broadcast) packet[10] = 0x80;
            if (type != 6) Write32(packet, 16, ClientWord);
            ClientMac.CopyTo(packet, 28);
            Write32(packet, 236, Cookie);
            int at = Options;
            packet[at++] = 53;
            packet[at++] = 1;
            packet[at++] = type;
            Option(packet, ref at, 54, ServerWord);
            if (type != 6)
            {
                Option(packet, ref at, 1, 0xfffffffc);
                Option(packet, ref at, 51, LeaseSeconds);
                Option(packet, ref at, 58, T1);
                Option(packet, ref at, 59, T2);
            }
            packet[at++] = 61;
            packet[at++] = 7;
            packet[at++] = 1;
            ClientMac.CopyTo(packet, at);
            packet[at + ClientMac.Length] = 255;
            return packet;
        }

        struct Request
        {
            public byte Type;
            public uint Xid, Address;
            public uint? Requested, SelectedServer;
        }

        static bool Parse(byte[] packet, int length, out Request request)
        {
            request = new Request();
            if (length < Options + 4 || packet[0] != 1 || packet[1] != 1 || packet[2] != 6 ||
                packet[3] != 0 || Read32(packet, 24) != 0 || Read32(packet, 236) != Cookie ||
                !HasClientMac(packet, 28)) return false;

            request.Xid = Read32(packet, 4);
            request.Address = Read32(packet, 12);
            bool clientId = false;
            for (int at = Options; at < length;)
            {
                byte code = packet[at++];
                if (code == 0) continue;
                if (code == 255) return request.Xid != 0 && request.Type != 0 && clientId;
                if (at == length) return false;
                int bytes = packet[at++];
                if (bytes > length - at) return false;
                switch (code)
                {
                    case 53:
                        if (bytes != 1 || request.Type != 0 || (packet[at] != 1 && packet[at] != 3)) return false;
                        request.Type = packet[at];
                        break;
                    case 50:
                        if (bytes != 4 || request.Requested.HasValue) return false;
                        request.Requested = Read32(packet, at);
                        break;
                    case 54:
                        if (bytes != 4 || request.SelectedServer.HasValue) return false;
                        request.SelectedServer = Read32(packet, at);
                        break;
                    case 61:
                        if (bytes != 7 || clientId || packet[at] != 1 || !HasClientMac(packet, at + 1)) return false;
                        clientId = true;
                        break;
                    case 52:
                        return false;
                }
                at += bytes;
            }
            return false;
        }

        byte[] Handle(byte[] packet, int length, IPAddress source, bool broadcast, long now, out string activity)
        {
            activity = null;
            Request r;
            if (!Parse(packet, length, out r)) return null;
            if (r.Address == 0 && source.Equals(IPAddress.Any) && broadcast)
            {
                if (r.Type == 1 && !r.SelectedServer.HasValue)
                {
                    offered = r.Xid;
                    activity = "DISCOVER -> OFFER";
                    return Reply(2, r.Xid, true);
                }
                if (r.Type == 3 && r.Xid == offered && r.Requested == ClientWord && r.SelectedServer == ServerWord)
                {
                    expires = now + LeaseSeconds * 1000;
                    activity = "REQUEST -> ACK";
                    return Reply(5, r.Xid, true);
                }
            }
            if (r.Type != 3 || r.Address != ClientWord || !source.Equals(Client) || now >= expires ||
                r.Requested.HasValue || r.SelectedServer.HasValue) return null;

            string stage = broadcast ? "REBIND" : "RENEW";
            if (mode == "Silence" || (mode == "Rebind" && !broadcast))
            {
                activity = stage + " -> deliberately unanswered";
                return null;
            }
            if (mode == "Nak")
            {
                activity = stage + " -> NAK";
                return Reply(6, r.Xid, true);
            }
            expires = now + LeaseSeconds * 1000;
            activity = stage + " -> ACK";
            return Reply(5, r.Xid, false);
        }

        static int FindCable(string alias, out string pending)
        {
            pending = null;
            foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (!String.Equals(nic.Name, alias, StringComparison.OrdinalIgnoreCase)) continue;
                if (nic.NetworkInterfaceType == NetworkInterfaceType.Loopback || nic.NetworkInterfaceType == NetworkInterfaceType.Wireless80211)
                    throw new InvalidOperationException(alias + " is " + nic.NetworkInterfaceType + ". Select the wired Ethernet adapter connected to the Pi.");
                if (nic.OperationalStatus != OperationalStatus.Up)
                {
                    pending = "Windows reports " + alias + " link " + nic.OperationalStatus +
                              ". Connect the cable and run ethup, then ethprobe on the Pi.";
                    return 0;
                }

                IPInterfaceProperties info = nic.GetIPProperties();
                foreach (GatewayIPAddressInformation gateway in info.GatewayAddresses)
                    if (gateway.Address.AddressFamily == AddressFamily.InterNetwork && !gateway.Address.Equals(IPAddress.Any))
                        throw new InvalidOperationException("The selected test adapter has an IPv4 gateway.");

                foreach (UnicastIPAddressInformation address in info.UnicastAddresses)
                {
                    if (!address.Address.Equals(Server)) continue;
                    if (!IPAddress.Parse("255.255.255.252").Equals(address.IPv4Mask))
                        throw new InvalidOperationException("Set 192.0.2.1 to prefix length 30 on " + alias + ".");
                    var state = address.DuplicateAddressDetectionState;
                    if (state == DuplicateAddressDetectionState.Preferred) return info.GetIPv4Properties().Index;
                    string status = "Windows reports 192.0.2.1/30 on " + alias + " as " + state + ". The test needs Preferred.";
                    if (state != DuplicateAddressDetectionState.Tentative) throw new InvalidOperationException(status);
                    pending = status;
                    return 0;
                }
                throw new InvalidOperationException("Assign 192.0.2.1/30 to " + alias + ".");
            }
            throw new InvalidOperationException("Ethernet adapter not found: " + alias);
        }

        public static int WaitForCable(string alias, int seconds)
        {
            Stopwatch clock = Stopwatch.StartNew();
            string previous = null;
            while (true)
            {
                string pending;
                int index = FindCable(alias, out pending);
                if (index != 0) return index;
                if (clock.Elapsed.TotalSeconds >= seconds) throw new InvalidOperationException(pending);
                if (pending != previous) { Console.WriteLine(pending); previous = pending; }
                Thread.Sleep(250);
            }
        }

        public static void Run(string alias, int index, string mode, int seconds)
        {
            DhcpFixture fixture = new DhcpFixture(mode);
            using (Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp))
            {
                socket.ExclusiveAddressUse = true;
                socket.EnableBroadcast = true;
                socket.ReceiveTimeout = 250;
                socket.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.PacketInformation, true);
                socket.Bind(new IPEndPoint(IPAddress.Any, 67));
                Console.WriteLine("DHCP fixture on {0} (ifIndex={1}), Windows=192.0.2.1 Pi=192.0.2.2 mode={2}", alias, index, mode);
                Console.WriteLine("Lease 80s. T1 25s. T2 55s. Start ethdhcp up on the Pi.");
                Stopwatch clock = Stopwatch.StartNew();
                byte[] packet = new byte[1472];
                int offers = 0, initialAcks = 0, leaseAcks = 0, naks = 0;
                while (clock.Elapsed.TotalSeconds < seconds)
                {
                    EndPoint remote = new IPEndPoint(IPAddress.Any, 0);
                    SocketFlags flags = SocketFlags.None;
                    IPPacketInformation info;
                    int length;
                    try { length = socket.ReceiveMessageFrom(packet, 0, packet.Length, ref flags, ref remote, out info); }
                    catch (SocketException e)
                    {
                        if (e.SocketErrorCode == SocketError.TimedOut || e.SocketErrorCode == SocketError.MessageSize) continue;
                        throw;
                    }
                    IPEndPoint peer = (IPEndPoint)remote;
                    bool broadcast = info.Address.Equals(IPAddress.Broadcast);
                    if (info.Interface != index || peer.Port != 68 || (!broadcast && !info.Address.Equals(Server)) ||
                        (flags & SocketFlags.Truncated) != 0) continue;

                    string activity;
                    byte[] reply = fixture.Handle(packet, length, peer.Address, broadcast, clock.ElapsedMilliseconds, out activity);
                    if (reply != null)
                    {
                        IPAddress target = reply[Options + 2] == 6 || peer.Address.Equals(IPAddress.Any) ? IPAddress.Broadcast : Client;
                        Send(socket, index, Server, new IPEndPoint(target, 68), reply);
                        if (reply[Options + 2] == 2) ++offers;
                        else if (reply[Options + 2] == 6) ++naks;
                        else if (peer.Address.Equals(IPAddress.Any)) ++initialAcks;
                        else ++leaseAcks;
                    }
                    if (activity != null) Console.WriteLine("{0,6:F1}s xid={1:x8} {2}", clock.Elapsed.TotalSeconds, Read32(packet, 4), activity);
                }
                Console.WriteLine("DHCP fixture closed. Offers={0} initial_acks={1} renewal_acks={2} naks={3}",
                                  offers, initialAcks, leaseAcks, naks);
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        struct WsaBuffer { public uint Length; public IntPtr Pointer; }

        [StructLayout(LayoutKind.Sequential)]
        struct WsaMessage
        {
            public IntPtr Address;
            public int AddressLength;
            public IntPtr Buffers;
            public uint BufferCount;
            public WsaBuffer Control;
            public uint Flags;
        }

        [DllImport("Ws2_32.dll", ExactSpelling = true)]
        static extern int WSASendMsg(IntPtr socket, ref WsaMessage message, uint flags, out uint sent, IntPtr overlapped, IntPtr completion);

        [DllImport("Ws2_32.dll", ExactSpelling = true)]
        static extern int WSAGetLastError();

        static void Send(Socket socket, int index, IPAddress source, IPEndPoint target, byte[] payload)
        {
            SocketAddress address = target.Serialize();
            byte[] nativeAddress = new byte[address.Size];
            for (int i = 0; i < address.Size; ++i) nativeAddress[i] = address[i];

            // WSACMSGHDR followed by IN_PKTINFO, with source address and interface.
            byte[] control = new byte[IntPtr.Size + 16];
            BitConverter.GetBytes(control.Length).CopyTo(control, 0);
            BitConverter.GetBytes((int)SocketOptionName.PacketInformation).CopyTo(control, IntPtr.Size + 4);
            source.GetAddressBytes().CopyTo(control, IntPtr.Size + 8);
            BitConverter.GetBytes(index).CopyTo(control, IntPtr.Size + 12);

            GCHandle name = default(GCHandle), data = default(GCHandle), list = default(GCHandle), ancillary = default(GCHandle);
            try
            {
                name = GCHandle.Alloc(nativeAddress, GCHandleType.Pinned);
                data = GCHandle.Alloc(payload, GCHandleType.Pinned);
                ancillary = GCHandle.Alloc(control, GCHandleType.Pinned);
                list = GCHandle.Alloc(new[] { new WsaBuffer { Length = (uint)payload.Length, Pointer = data.AddrOfPinnedObject() } }, GCHandleType.Pinned);
                WsaMessage message = new WsaMessage
                {
                    Address = name.AddrOfPinnedObject(), AddressLength = nativeAddress.Length,
                    Buffers = list.AddrOfPinnedObject(), BufferCount = 1,
                    Control = new WsaBuffer { Length = (uint)control.Length, Pointer = ancillary.AddrOfPinnedObject() }
                };
                uint sent;
                if (WSASendMsg(socket.Handle, ref message, 0, out sent, IntPtr.Zero, IntPtr.Zero) != 0)
                    throw new SocketException(WSAGetLastError());
                if (sent != payload.Length) throw new InvalidOperationException("Incomplete DHCP reply.");
            }
            finally
            {
                if (list.IsAllocated) list.Free();
                if (ancillary.IsAllocated) ancillary.Free();
                if (data.IsAllocated) data.Free();
                if (name.IsAllocated) name.Free();
            }
        }

        static byte[] TestRequest(byte type, uint xid, bool selecting, uint address)
        {
            byte[] packet = new byte[300];
            packet[0] = 1; packet[1] = 1; packet[2] = 6;
            Write32(packet, 4, xid);
            Write32(packet, 12, address);
            ClientMac.CopyTo(packet, 28);
            Write32(packet, 236, Cookie);
            int at = Options;
            packet[at++] = 53; packet[at++] = 1; packet[at++] = type;
            if (selecting) { Option(packet, ref at, 50, ClientWord); Option(packet, ref at, 54, ServerWord); }
            packet[at++] = 61; packet[at++] = 7; packet[at++] = 1;
            ClientMac.CopyTo(packet, at);
            packet[at + ClientMac.Length] = 255;
            return packet;
        }

        static void Check(bool result, string where)
        {
            if (!result) throw new InvalidOperationException("DHCP fixture self-test failed: " + where);
        }

        public static void SelfTest()
        {
            foreach (string mode in new[] { "Ack", "Rebind", "Nak", "Silence" })
            {
                DhcpFixture fixture = new DhcpFixture(mode);
                string activity;
                byte[] discover = TestRequest(1, 123, false, 0);
                byte[] wrong = (byte[])discover.Clone(); wrong[28] ^= 1;
                Check(fixture.Handle(wrong, wrong.Length, IPAddress.Any, true, 0, out activity) == null, "foreign MAC");
                Check(fixture.Handle(discover, Options + 3, IPAddress.Any, true, 0, out activity) == null, "truncated request");
                byte[] offer = fixture.Handle(discover, discover.Length, IPAddress.Any, true, 0, out activity);
                Check(offer != null && offer[242] == 2 && Read32(offer, 4) == 123 && Read32(offer, 16) == ClientWord, "offer");
                byte[] select = TestRequest(3, 124, true, 0);
                Check(fixture.Handle(select, select.Length, IPAddress.Any, true, 10, out activity) == null, "wrong transaction");
                Write32(select, 4, 123);
                byte[] ack = fixture.Handle(select, select.Length, IPAddress.Any, true, 20, out activity);
                Check(ack != null && ack[242] == 5 && fixture.expires == 80020, "select ACK");
                byte[] renew = TestRequest(3, 125, false, ClientWord);
                Check(fixture.Handle(renew, renew.Length, Server, false, 25020, out activity) == null, "wrong source");
                byte[] retry = fixture.Handle(renew, renew.Length, Client, false, 25020, out activity);
                int expected = mode == "Ack" ? 5 : mode == "Nak" ? 6 : 0;
                Check((retry == null ? 0 : retry[242]) == expected, mode + " renew");
                if (mode == "Rebind")
                {
                    Write32(renew, 4, 126);
                    byte[] rebind = fixture.Handle(renew, renew.Length, Client, true, 55020, out activity);
                    Check(rebind != null && rebind[242] == 5 && Read32(rebind, 4) == 126 && fixture.expires == 135020, "rebind ACK");
                }
                if (mode == "Silence")
                    Check(fixture.Handle(renew, renew.Length, Client, true, 80020, out activity) == null && activity == null, "expired");
            }
            Console.WriteLine("PASS: one-client DHCP offer, ACK, renewal, rebinding, NAK, silence and input checks.");
            TestTransport();
        }

        static void TestTransport()
        {
            using (Socket sender = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp))
            using (Socket receiver = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp))
            {
                sender.Bind(new IPEndPoint(IPAddress.Any, 0));
                receiver.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.PacketInformation, true);
                receiver.Bind(new IPEndPoint(IPAddress.Loopback, 0));
                receiver.ReceiveTimeout = 1500;
                byte[] fixture = Reply(5, 123, false);
                int index = NetworkInterface.LoopbackInterfaceIndex;
                Send(sender, index, IPAddress.Loopback, (IPEndPoint)receiver.LocalEndPoint, fixture);
                EndPoint peer = new IPEndPoint(IPAddress.Any, 0);
                SocketFlags flags = SocketFlags.None;
                IPPacketInformation info;
                byte[] packet = new byte[400];
                int length = receiver.ReceiveMessageFrom(packet, 0, packet.Length, ref flags, ref peer, out info);
                Check(length == fixture.Length && Read32(packet, 4) == 123 && ((IPEndPoint)peer).Address.Equals(IPAddress.Loopback) &&
                      info.Interface == index, "interface-bound Winsock loopback");
                Console.WriteLine("PASS: Winsock pinned output and ingress interface on Windows loopback.");
            }
        }
    }
}
