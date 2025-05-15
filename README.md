# 🚀 YuB-X Roblox API  
**Version:** `version-3c1b78b767674c66`  
**UNC Ready – 35 APIs Integrated**

---

## 💉 How to Inject

1. Use our official manual map injector:  
   👉 [YuB-X MMP Injector](https://github.com/YuB-W/RBX-MMP-Injector/tree/main)

2. Run Roblox, inject `yubx.dll` via the injector.

3. Once injected, you can send any Luau script to be executed remotely using the C# client below.

---

## 📡 Sending Scripts from C# (.NET)

```csharp
using System;
using System.Net.Sockets;
using System.Text;

class ScriptSender
{
    const string Host = "127.0.0.1"; // Set to target IP if remote
    const int Port = 5454;

    static void Main(string[] args)
    {
        string script = "-- YuB-X Payload\nprint('Injected successfully!')";

        try
        {
            using (TcpClient client = new TcpClient(Host, Port))
            using (NetworkStream stream = client.GetStream())
            {
                byte[] scriptBytes = Encoding.UTF8.GetBytes(script);
                int scriptLength = scriptBytes.Length;

                // Convert to network byte order (big-endian)
                byte[] lengthBytes = BitConverter.GetBytes(scriptLength);
                if (BitConverter.IsLittleEndian)
                    Array.Reverse(lengthBytes);

                stream.Write(lengthBytes, 0, 4);
                stream.Write(scriptBytes, 0, scriptBytes.Length);

                Console.WriteLine("[✓] Script sent to YuB-X server");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("[✗] Failed to send script: " + ex.Message);
        }
    }
}
