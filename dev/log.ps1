# Collects Windhawk mod logs (OutputDebugString) for a few seconds, like DebugView.
# Usage: powershell -File dev/log.ps1 [-Seconds 10] [-Filter messy-toasts]
# Only one debug listener can run at a time: close DebugView/DbgViewMini first.
param([int]$Seconds = 10, [string]$Filter = '')

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Text;

public static class DbWin {
	public static List<string> Listen(int seconds) {
		var lines = new List<string>();
		bool created;
		using (var bufferReady = new EventWaitHandle(false, EventResetMode.AutoReset, "DBWIN_BUFFER_READY", out created))
		using (var dataReady = new EventWaitHandle(false, EventResetMode.AutoReset, "DBWIN_DATA_READY", out created))
		using (var file = MemoryMappedFile.CreateOrOpen("DBWIN_BUFFER", 4096))
		using (var view = file.CreateViewAccessor()) {
			var end = DateTime.UtcNow.AddSeconds(seconds);
			bufferReady.Set();
			while (DateTime.UtcNow < end) {
				if (!dataReady.WaitOne(200)) continue;
				int pid = view.ReadInt32(0);
				var bytes = new byte[4092];
				view.ReadArray(4, bytes, 0, bytes.Length);
				int len = Array.IndexOf(bytes, (byte)0);
				if (len < 0) len = bytes.Length;
				lines.Add(DateTime.Now.ToString("HH:mm:ss.fff") + " " + pid + " " + Encoding.Default.GetString(bytes, 0, len).TrimEnd());
				bufferReady.Set();
			}
		}
		return lines;
	}
}
'@

[DbWin]::Listen($Seconds) | Where-Object { $_ -like "*$Filter*" }
