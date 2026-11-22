// Controller Polling Rate - measures how often a gamepad (PS5 DualSense and others) sends input
// reports to Windows. It only LISTENS: it uses the standard Windows Raw Input API and never writes
// to the controller, its firmware, drivers or any Windows setting.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace ControllerPollingRate
{
    static class Native
    {
        public const int WM_INPUT = 0x00FF;
        public const uint RIDEV_INPUTSINK = 0x00000100;
        public const uint RID_HEADER = 0x10000005;
        public const uint RIDI_DEVICENAME = 0x20000007;
        public const uint RIM_TYPEHID = 2;

        [StructLayout(LayoutKind.Sequential)]
        public struct RAWINPUTDEVICE
        {
            public ushort UsagePage;
            public ushort Usage;
            public uint Flags;
            public IntPtr Target;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct RAWINPUTHEADER
        {
            public uint dwType;
            public uint dwSize;
            public IntPtr hDevice;
            public IntPtr wParam;
        }

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool RegisterRawInputDevices([In] RAWINPUTDEVICE[] devices, uint count, uint size);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetRawInputData(IntPtr hRawInput, uint command, IntPtr data, ref uint size, uint headerSize);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetRawInputDeviceInfo(IntPtr hDevice, uint command, IntPtr data, ref uint size);
    }

    class DeviceStats
    {
        public IntPtr Handle;
        public string Path = "";
        public string Name = "";
        public string Connection = "";
        public readonly Queue<long> Recent = new Queue<long>();   // arrival times in the last second
        public readonly Queue<double> Intervals = new Queue<double>(); // last N gaps, in ms
        public long Total;
        public long Last;
        public double MinMs = double.MaxValue, MaxMs;

        public void Reset()
        {
            lock (this)
            {
                Recent.Clear(); Intervals.Clear(); Total = 0; Last = 0; MinMs = double.MaxValue; MaxMs = 0;
            }
        }
    }

    class MainForm : Form
    {
        readonly Dictionary<IntPtr, DeviceStats> devices = new Dictionary<IntPtr, DeviceStats>();
        readonly Stopwatch clock = Stopwatch.StartNew();
        readonly ListView list = new ListView();
        readonly Label status = new Label();
        readonly Timer timer = new Timer();

        public MainForm()
        {
            Text = "Controller Polling Rate";
            ClientSize = new Size(780, 300);
            MinimumSize = new Size(640, 260);
            Font = new Font("Segoe UI", 9.5f);

            status.Dock = DockStyle.Top;
            status.Height = 56;
            status.Padding = new Padding(10, 8, 10, 0);
            status.Text = "Plug in your controller (USB or Bluetooth) and move a stick or press a button.\r\n" +
                          "This tool only measures how often the controller reports. It never changes the controller, drivers or Windows.";

            list.Dock = DockStyle.Fill;
            list.View = View.Details;
            list.FullRowSelect = true;
            list.GridLines = true;
            list.Columns.Add("Controller", 190);
            list.Columns.Add("Connection", 90);
            list.Columns.Add("Rate (Hz)", 80);
            list.Columns.Add("Avg gap (ms)", 95);
            list.Columns.Add("Min gap", 70);
            list.Columns.Add("Max gap", 70);
            list.Columns.Add("Reports", 90);

            var bottom = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 42, Padding = new Padding(8, 6, 8, 6) };
            var reset = new Button { Text = "Reset stats", AutoSize = true };
            reset.Click += (s, e) => { foreach (var d in devices.Values) d.Reset(); };
            var top = new CheckBox { Text = "Always on top", AutoSize = true, Margin = new Padding(14, 6, 0, 0) };
            top.CheckedChanged += (s, e) => TopMost = top.Checked;
            bottom.Controls.Add(reset);
            bottom.Controls.Add(top);

            Controls.Add(list);
            Controls.Add(bottom);
            Controls.Add(status);

            timer.Interval = 250;
            timer.Tick += (s, e) => RefreshList();
            timer.Start();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            // Gamepad (generic desktop, usage 5) and joystick (usage 4). INPUTSINK = keep receiving
            // reports while this window is in the background, which is how you test during a game.
            var rid = new[]
            {
                new Native.RAWINPUTDEVICE { UsagePage = 1, Usage = 5, Flags = Native.RIDEV_INPUTSINK, Target = Handle },
                new Native.RAWINPUTDEVICE { UsagePage = 1, Usage = 4, Flags = Native.RIDEV_INPUTSINK, Target = Handle },
            };
            if (!Native.RegisterRawInputDevices(rid, (uint)rid.Length, (uint)Marshal.SizeOf(typeof(Native.RAWINPUTDEVICE))))
                status.Text = "Could not register for controller input (error " + Marshal.GetLastWin32Error() + ").";
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == Native.WM_INPUT)
            {
                long now = clock.ElapsedTicks;
                OnReport(m.LParam, now);
            }
            base.WndProc(ref m);
        }

        void OnReport(IntPtr hRawInput, long now)
        {
            uint size = (uint)Marshal.SizeOf(typeof(Native.RAWINPUTHEADER));
            IntPtr buf = Marshal.AllocHGlobal((int)size);
            try
            {
                if (Native.GetRawInputData(hRawInput, Native.RID_HEADER, buf, ref size, size) == uint.MaxValue) return;
                var header = (Native.RAWINPUTHEADER)Marshal.PtrToStructure(buf, typeof(Native.RAWINPUTHEADER));
                if (header.dwType != Native.RIM_TYPEHID) return;

                DeviceStats d;
                if (!devices.TryGetValue(header.hDevice, out d))
                {
                    d = Describe(header.hDevice);
                    devices[header.hDevice] = d;
                }
                lock (d)
                {
                    if (d.Last != 0)
                    {
                        double ms = (now - d.Last) * 1000.0 / Stopwatch.Frequency;
                        d.Intervals.Enqueue(ms);
                        while (d.Intervals.Count > 500) d.Intervals.Dequeue();
                        if (ms < d.MinMs) d.MinMs = ms;
                        if (ms > d.MaxMs) d.MaxMs = ms;
                    }
                    d.Last = now;
                    d.Recent.Enqueue(now);
                    d.Total++;
                }
            }
            finally { Marshal.FreeHGlobal(buf); }
        }

        static DeviceStats Describe(IntPtr handle)
        {
            var d = new DeviceStats { Handle = handle };
            uint chars = 0;
            Native.GetRawInputDeviceInfo(handle, Native.RIDI_DEVICENAME, IntPtr.Zero, ref chars);
            if (chars > 0)
            {
                IntPtr p = Marshal.AllocHGlobal((int)chars * 2);
                try
                {
                    if (Native.GetRawInputDeviceInfo(handle, Native.RIDI_DEVICENAME, p, ref chars) != uint.MaxValue)
                        d.Path = Marshal.PtrToStringUni(p) ?? "";
                }
                finally { Marshal.FreeHGlobal(p); }
            }
            string up = d.Path.ToUpperInvariant();
            // Bluetooth HID devices carry the Bluetooth HID service id; USB ones have plain VID_xxxx.
            if (up.Contains("{00001124") || up.Contains("BTHENUM") || up.Contains("BTHLE")) d.Connection = "Bluetooth";
            else if (up.Contains("VID_") || up.Contains("USB")) d.Connection = "USB";
            else d.Connection = "Unknown";

            var m = Regex.Match(up, @"VID[_&](?:0002)?([0-9A-F]{4}).*?PID[_&]([0-9A-F]{4})");
            string vid = m.Success ? m.Groups[1].Value : "";
            string pid = m.Success ? m.Groups[2].Value : "";
            if (vid == "054C" && pid == "0CE6") d.Name = "PS5 DualSense";
            else if (vid == "054C" && pid == "0DF2") d.Name = "PS5 DualSense Edge";
            else if (vid == "054C" && (pid == "05C4" || pid == "09CC")) d.Name = "PS4 DualShock 4";
            else d.Name = m.Success ? "Gamepad (" + vid + ":" + pid + ")" : "Gamepad";
            return d;
        }

        void RefreshList()
        {
            long now = clock.ElapsedTicks;
            long window = Stopwatch.Frequency; // 1 second
            var rows = new List<string[]>();
            foreach (var d in devices.Values.ToList())
            {
                double hz, avg;
                string min, max;
                long total;
                lock (d)
                {
                    while (d.Recent.Count > 0 && now - d.Recent.Peek() > window) d.Recent.Dequeue();
                    hz = d.Recent.Count;
                    avg = d.Intervals.Count > 0 ? d.Intervals.Average() : 0;
                    min = d.MinMs == double.MaxValue ? "-" : d.MinMs.ToString("0.00");
                    max = d.Intervals.Count > 0 ? d.MaxMs.ToString("0.00") : "-";
                    total = d.Total;
                }
                rows.Add(new[] { d.Name, d.Connection, hz.ToString("0"), avg > 0 ? avg.ToString("0.00") : "-", min, max, total.ToString("N0") });
            }

            list.BeginUpdate();
            list.Items.Clear();
            foreach (var r in rows) list.Items.Add(new ListViewItem(r));
            list.EndUpdate();
            if (rows.Count > 0)
                status.Text = "Rate = reports received in the last second. Gaps are the time between reports.\r\n" +
                              "Move a stick (some controllers send less when idle). Measuring only - nothing is changed.";
        }
    }

    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
