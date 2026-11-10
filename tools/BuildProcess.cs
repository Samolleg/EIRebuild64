using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

// A suspended root is assigned before it can spawn workers. The job contains
// only this invocation; closing it also handles orphaned MSBuild descendants.
public static class EIBuildProcess {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct StartupInfo {
        public int cb; public string reserved, desktop, title;
        public int x,y,xSize,ySize,xChars,yChars,fill,flags;
        public short show, reservedSize; public IntPtr reservedPtr, input, output, error;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo { public IntPtr process, thread; public int pid, tid; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateProcess(string app, StringBuilder command, IntPtr pa, IntPtr ta,
        bool inherit, uint flags, IntPtr environment, string directory, ref StartupInfo si, out ProcessInfo pi);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle, uint timeout);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateJobObject(IntPtr job, uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static int Run(string app, string arguments, string directory) {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero) throw new Win32Exception();
        ProcessInfo pi = new ProcessInfo();
        bool assigned = false;
        try {
            StartupInfo si = new StartupInfo(); si.cb = Marshal.SizeOf(si);
            if (!CreateProcess(app, new StringBuilder("\"" + app + "\" " + arguments),
                IntPtr.Zero, IntPtr.Zero, true, 4, IntPtr.Zero, directory, ref si, out pi)) throw new Win32Exception();
            if (!AssignProcessToJobObject(job, pi.process)) throw new Win32Exception();
            assigned = true;
            Console.WriteLine("MSBuild root PID: " + pi.pid + "; descendants confined to private job.");
            if (ResumeThread(pi.thread) == UInt32.MaxValue) throw new Win32Exception();
            if (WaitForSingleObject(pi.process, UInt32.MaxValue) != 0) throw new Win32Exception();
            uint code;
            if (!GetExitCodeProcess(pi.process, out code)) throw new Win32Exception();
            return unchecked((int)code);
        } finally {
            // Job membership tracks exact process identities, including orphaned nodes.
            // No process-name search or termination outside this invocation.
            if (assigned) {
                if (!TerminateJobObject(job, 1)) throw new Win32Exception();
                if (WaitForSingleObject(job, 30000) != 0) throw new Exception("Build job did not drain.");
                Console.WriteLine("Private MSBuild job drained; no owned workers remain.");
            } else if (pi.process != IntPtr.Zero) { TerminateProcess(pi.process, 1); }
            if (pi.thread != IntPtr.Zero) CloseHandle(pi.thread);
            if (pi.process != IntPtr.Zero) CloseHandle(pi.process);
            CloseHandle(job);
        }
    }
}
