using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace MultiLaunch
{
    internal static class Program
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct STARTUPINFO
        {
            public int cb;
            public string lpReserved, lpDesktop, lpTitle;
            public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2;
            public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_INFORMATION
        {
            public IntPtr hProcess, hThread;
            public int dwProcessId, dwThreadId;
        }

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CreateProcess(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit,
            uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr addr, IntPtr size, int type, int protect);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr written);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern IntPtr GetProcAddress(IntPtr mod, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateRemoteThread(IntPtr h, IntPtr attr, IntPtr stack, IntPtr start, IntPtr param, int flags, IntPtr id);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr h, uint ms);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool ResumeThread(IntPtr h);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CreateHardLink(string newName, string existingName, IntPtr sa);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GetEnvironmentStringsW();
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeEnvironmentStringsW(IntPtr block);

        private const uint CREATE_SUSPENDED = 0x00000004;
        private const uint CREATE_UNICODE_ENVIRONMENT = 0x00000400;
        private const int MEM_COMMIT = 0x1000, PAGE_READWRITE = 0x04;

        private static int Main(string[] args)
        {
            try
            {
                // 免参数：双击即开一个新实例（自动分配 profile）
                string exe, profile;
                if (args.Length == 0)
                {
                    exe = @"D:\steam\steamapps\common\Astral Party\8vJXn6CN\AstralParty_CN.exe";
                    profile = ChooseAccount(@"D:\MultiProfiles");
                    if (profile == null) return 0;
                    Console.WriteLine("选择账号: " + profile);
                }
                else if (args.Length == 1)
                {
                    exe = @"D:\steam\steamapps\common\Astral Party\8vJXn6CN\AstralParty_CN.exe";
                    profile = Path.GetFullPath(args[0]);
                }
                else
                {
                    exe = args[0];
                    profile = Path.GetFullPath(args[1]);
                }
                SeedSharedCaches(profile);
                string dll = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "UserDataRedirect.dll");
                if (!File.Exists(exe)) { Console.WriteLine("找不到游戏: " + exe); return 1; }
                if (!File.Exists(dll)) { Console.WriteLine("找不到 DLL: " + dll); return 1; }
                Directory.CreateDirectory(Path.Combine(profile, "AppData", "LocalLow"));

                // 单实例检查按“进程名”判断：给每个 profile 一个硬链接 exe 名
                string runExe = exe;
                try
                {
                    int h = 0;
                    foreach (char c in profile.ToLowerInvariant()) h = h * 31 + c;
                    string link = Path.Combine(Path.GetDirectoryName(exe),
                        "AstralParty_CN_" + Math.Abs(h).ToString("X8") + ".exe");
                    if (!File.Exists(link))
                    {
                        if (!CreateHardLink(link, exe, IntPtr.Zero))
                            File.Copy(exe, link, true);
                    }
                    // Unity 需要 <exe名>_Data 目录：用 junction 指回真实数据目录
                    string realData = Path.Combine(Path.GetDirectoryName(exe),
                        Path.GetFileNameWithoutExtension(exe) + "_Data");
                    string linkData = Path.Combine(Path.GetDirectoryName(exe),
                        Path.GetFileNameWithoutExtension(link) + "_Data");
                    if (!Directory.Exists(linkData) && Directory.Exists(realData))
                    {
                        var mklink = new ProcessStartInfo("cmd.exe",
                            "/c mklink /J \"" + linkData + "\" \"" + realData + "\"")
                        {
                            UseShellExecute = false,
                            CreateNoWindow = true,
                            RedirectStandardOutput = true,
                            RedirectStandardError = true
                        };
                        Process.Start(mklink);
                        System.Threading.Thread.Sleep(300);
                    }
                    runExe = link;
                    Console.WriteLine("实例 exe: " + Path.GetFileName(link));
                }
                catch (Exception le) { Console.WriteLine("创建实例 exe 失败: " + le.Message); }

                var sb = new StringBuilder("\"" + runExe + "\"");
                for (int i = 2; i < args.Length; i++) sb.Append(' ').Append(args[i]);

                var si = new STARTUPINFO();
                si.cb = Marshal.SizeOf(si);
                PROCESS_INFORMATION pi;
                IntPtr env = BuildEnv("ASTRAL_PROFILE_DIR", profile, "ASTRAL_LOG", Path.Combine(profile, "redirect.log"));
                if (!CreateProcess(runExe, sb, IntPtr.Zero, IntPtr.Zero, false,
                    CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                    env, Path.GetDirectoryName(exe), ref si, out pi))
                {
                    Console.WriteLine("CreateProcess 失败: " + Marshal.GetLastWin32Error());
                    return 1;
                }

                Console.WriteLine("已启动 pid=" + pi.dwProcessId + "（挂起中），注入 DLL…");
                Inject(pi.hProcess, dll);
                ResumeThread(pi.hThread);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                try { System.IO.File.WriteAllText(System.IO.Path.Combine(profile, "instance.lock"), pi.dwProcessId.ToString()); } catch { }
                Console.WriteLine("完成。配置目录: " + profile);
                return 0;
            }
            catch (Exception e)
            {
                Console.WriteLine("失败: " + e.Message);
                return 2;
            }
        }

        /// <summary>把大缓存（资源包）做成指向官方目录的 junction，避免每个实例重新下载；BnSdk 保持独立。</summary>
        private static void SeedSharedCaches(string profile)
        {
            try
            {
                string official = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                    "AppData", "LocalLow", "feimo", "AstralParty_CN");
                string target = Path.Combine(profile, "AppData", "LocalLow", "feimo", "AstralParty_CN");
                Directory.CreateDirectory(target);
                foreach (var name in new[] { "com.unity.addressables", "NextPack", "NextPack.full-33", "Temp" })
                {
                    string src = Path.Combine(official, name);
                    string dst = Path.Combine(target, name);
                    if (!Directory.Exists(src) || Directory.Exists(dst)) continue;
                    var psi = new ProcessStartInfo("cmd.exe",
                        "/c mklink /J \"" + dst + "\" \"" + src + "\"")
                    {
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true
                    };
                    Process.Start(psi);
                    Console.WriteLine("link cache: " + name);
                }
                System.Threading.Thread.Sleep(400);
            }
            catch (Exception e) { Console.WriteLine("seed caches failed: " + e.Message); }
        }

        /// <summary>列出已保存账号，让用户选择；0 = 新建。</summary>
        private static string ChooseAccount(string root)
        {
            Directory.CreateDirectory(root);
            MigrateDirs(root);
            var dirs = new System.Collections.Generic.List<string>(Directory.GetDirectories(root));
            dirs.Sort();

            Console.WriteLine("已保存账号：");
            for (int i = 0; i < dirs.Count; i++)
            {
                string name = ReadName(dirs[i]);
                bool used = IsUsed(dirs[i]);
                string dirName = Path.GetFileName(dirs[i]);
                string extra = dirName != name ? "  (" + dirName + ")" : "";
                Console.WriteLine("  " + (i + 1) + ") " + name + extra + (used ? "  [运行中]" : ""));
            }
            Console.WriteLine("  0) 新建账号");
            Console.Write("请选择: ");

            string line = null;
            try { line = Console.ReadLine(); } catch { }
            int sel;
            if (!int.TryParse(line, out sel))
            {
                Console.WriteLine("未选择，退出。");
                return null;
            }

            if (sel >= 1 && sel <= dirs.Count) return dirs[sel - 1];

            Console.Write("新账号名称: ");
            string newName = null;
            try { newName = Console.ReadLine(); } catch { }
            if (string.IsNullOrWhiteSpace(newName)) newName = "账号" + (dirs.Count + 1);

            return CreateAccountDir(root, newName);
        }

        /// <summary>从 BnSdk 日志里读登录账号名（user_name）。</summary>
        private static string ReadBnSdkName(string dir)
        {
            try
            {
                string baseDir = Path.Combine(dir, "AppData", "LocalLow", "feimo", "AstralParty_CN", "BnSdk");
                if (!Directory.Exists(baseDir)) return null;
                FileInfo newest = null;
                foreach (var f in Directory.GetFiles(baseDir, "*.log", SearchOption.AllDirectories))
                {
                    var fi = new FileInfo(f);
                    if (newest == null || fi.LastWriteTimeUtc > newest.LastWriteTimeUtc) newest = fi;
                }
                if (newest == null) return null;
                string text = File.ReadAllText(newest.FullName);
                var m = System.Text.RegularExpressions.Regex.Match(text, "\"user_name\"\\s*:\\s*\"([^\"]+)\"");
                if (m.Success) return m.Groups[1].Value;
            }
            catch { }
            return null;
        }

        /// <summary>用账号名做目录名（去非法字符，重名自动加序号）。</summary>
        private static string CreateAccountDir(string root, string rawName)
        {
            string name = SanitizeName(rawName);
            if (name.Length == 0) name = "acc_" + DateTime.Now.ToString("MMdd_HHmmss");
            string dir = Path.Combine(root, name);
            int n = 2;
            while (Directory.Exists(dir)) { dir = Path.Combine(root, name + "_" + n); n++; }
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "name.txt"), rawName.Trim());
            return dir;
        }

        private static string SanitizeName(string raw)
        {
            if (string.IsNullOrEmpty(raw)) return "";
            var sb = new System.Text.StringBuilder();
            foreach (char c in raw.Trim())
            {
                if (Array.IndexOf(Path.GetInvalidFileNameChars(), c) >= 0) continue;
                if (c == '.' || c == ' ') continue;
                sb.Append(c);
            }
            string s = sb.ToString();
            return s.Length > 40 ? s.Substring(0, 40) : s;
        }

        /// <summary>把历史的时间戳目录重命名成账号名。</summary>
        private static void MigrateDirs(string root)
        {
            try
            {
                foreach (var dir in Directory.GetDirectories(root))
                {
                    string name = Path.GetFileName(dir);
                    if (!name.StartsWith("acc_")) continue;
                    string label = ReadLabel(dir);
                    if (string.IsNullOrEmpty(label)) continue;
                    string target = Path.Combine(root, SanitizeName(label));
                    if (target.Length == 0 || Directory.Exists(target)) continue;
                    try { Directory.Move(dir, target); } catch { }
                }
            }
            catch { }
        }

        private static string ReadLabel(string dir)
        {
            try
            {
                string f = Path.Combine(dir, "name.txt");
                if (File.Exists(f)) return File.ReadAllText(f).Trim();
            }
            catch { }
            return null;
        }

        private static string ReadName(string dir)
        {
            string label = ReadLabel(dir);
            if (!string.IsNullOrEmpty(label)) return label;
            string bnsdk = ReadBnSdkName(dir);
            if (!string.IsNullOrEmpty(bnsdk)) return bnsdk;
            try
            {
                string f = Path.Combine(dir, "name.txt");
                if (File.Exists(f)) return File.ReadAllText(f).Trim();
            }
            catch { }
            return Path.GetFileName(dir);
        }

        private static bool IsUsed(string dir)
        {
            try
            {
                string f = Path.Combine(dir, "instance.lock");
                if (!File.Exists(f)) return false;
                int pid;
                if (!int.TryParse(File.ReadAllText(f).Trim(), out pid)) return false;
                return Process.GetProcessById(pid) != null;
            }
            catch { return false; }
        }

        /// <summary>选一个没被占用的 profile 目录（v1、v2、…）。</summary>
        private static string AutoProfile(string root)
        {
            Directory.CreateDirectory(root);
            for (int i = 1; i < 100; i++)
            {
                string dir = Path.Combine(root, "v" + i);
                string lockFile = Path.Combine(dir, "instance.lock");
                bool used = false;
                try
                {
                    if (File.Exists(lockFile))
                    {
                        int pid;
                        if (int.TryParse(File.ReadAllText(lockFile).Trim(), out pid))
                            used = Process.GetProcessById(pid) != null;
                    }
                }
                catch { used = false; }
                if (used) continue;
                Directory.CreateDirectory(dir);
                return dir;
            }
            return Path.Combine(root, "v" + DateTime.Now.Ticks);
        }

        private static IntPtr BuildEnv(string key, string value, string key2, string value2)
        {
            IntPtr raw = GetEnvironmentStringsW();
            var parts = new System.Collections.Generic.List<string>();
            if (raw != IntPtr.Zero)
            {
                int off = 0;
                while (true)
                {
                    IntPtr p = IntPtr.Add(raw, off * 2);
                    string s = Marshal.PtrToStringUni(p);
                    if (string.IsNullOrEmpty(s)) break;
                    if (!s.StartsWith(key + "=", StringComparison.OrdinalIgnoreCase)) parts.Add(s);
                    off += s.Length + 1;
                }
                FreeEnvironmentStringsW(raw);
            }
            parts.Add(key + "=" + value);
            if (!string.IsNullOrEmpty(key2)) parts.Add(key2 + "=" + value2);
            string block = string.Join("\0", parts) + "\0\0";
            byte[] bytes = Encoding.Unicode.GetBytes(block);
            IntPtr mem = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, mem, bytes.Length);
            return mem;
        }

        private static void Inject(IntPtr process, string dll)
        {
            byte[] bytes = Encoding.Unicode.GetBytes(dll + "\0");
            IntPtr mem = VirtualAllocEx(process, IntPtr.Zero, (IntPtr)bytes.Length, MEM_COMMIT, PAGE_READWRITE);
            if (mem == IntPtr.Zero) { Console.WriteLine("VirtualAllocEx 失败"); return; }
            IntPtr written;
            WriteProcessMemory(process, mem, bytes, (IntPtr)bytes.Length, out written);

            IntPtr k32 = GetModuleHandle("kernel32.dll");
            IntPtr loadLib = GetProcAddress(k32, "LoadLibraryW");
            IntPtr thread = CreateRemoteThread(process, IntPtr.Zero, IntPtr.Zero, loadLib, mem, 0, IntPtr.Zero);
            if (thread == IntPtr.Zero) { Console.WriteLine("CreateRemoteThread 失败: " + Marshal.GetLastWin32Error()); return; }
            WaitForSingleObject(thread, 10000);
            CloseHandle(thread);
        }
    }
}
