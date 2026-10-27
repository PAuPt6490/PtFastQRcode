// Test harness: emits QR matrices for cross-checking against reference implementations.
// Usage: QrTest.exe <cases.txt> auto
//        QrTest.exe <cases.txt> forced <L|M|Q|H>
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using FastQRcode.Qr;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("usage: QrTest <cases.txt> auto | forced <L|M|Q|H>");
            return 2;
        }

        string[] lines = File.ReadAllLines(args[0], Encoding.UTF8);
        string mode = args[1];
        EccLevel forcedEcl = EccLevel.Low;
        if (mode == "forced" && args.Length > 2)
        {
            switch (args[2].ToUpperInvariant())
            {
                case "L": forcedEcl = EccLevel.Low; break;
                case "M": forcedEcl = EccLevel.Medium; break;
                case "Q": forcedEcl = EccLevel.Quartile; break;
                case "H": forcedEcl = EccLevel.High; break;
                default:
                    Console.Error.WriteLine("bad ecl");
                    return 2;
            }
        }

        StringBuilder sb = new StringBuilder();
        int index = 0;
        foreach (string raw in lines)
        {
            if (raw.Length == 0)
                continue;
            string text = raw == "-" ? "" : Encoding.UTF8.GetString(Convert.FromBase64String(raw));

            if (mode == "auto")
            {
                foreach (EccLevel ecl in new[] { EccLevel.Low, EccLevel.Medium, EccLevel.Quartile, EccLevel.High })
                {
                    try
                    {
                        QrCode qr = QrCode.EncodeText(text, ecl);
                        Emit(sb, index, ecl, qr);
                    }
                    catch (DataTooLongException)
                    {
                        sb.AppendLine("CASE " + index + " " + ecl + " ERROR too_long");
                    }
                }
            }
            else
            {
                int version;
                try
                {
                    version = QrCode.EncodeText(text, forcedEcl).Version;
                }
                catch (DataTooLongException)
                {
                    sb.AppendLine("CASE " + index + " " + forcedEcl + " ERROR too_long");
                    index++;
                    continue;
                }
                for (int mask = 0; mask < 8; mask++)
                {
                    QrCode qr = QrCode.EncodeText(text, forcedEcl, version, version, mask, false);
                    Emit(sb, index, forcedEcl, qr);
                }
            }
            index++;
        }

        File.WriteAllText(args[args.Length - 1], sb.ToString(), new UTF8Encoding(false));
        Console.WriteLine("cases=" + index);
        return 0;
    }

    private static void Emit(StringBuilder sb, int index, EccLevel ecl, QrCode qr)
    {
        sb.Append("CASE ").Append(index).Append(' ').Append(ecl)
          .Append(" V ").Append(qr.Version)
          .Append(" M ").Append(qr.Mask)
          .Append(" S ").Append(qr.Size)
          .AppendLine();
        for (int y = 0; y < qr.Size; y++)
        {
            for (int x = 0; x < qr.Size; x++)
                sb.Append(qr.GetModule(x, y) ? '1' : '0');
            sb.AppendLine();
        }
    }
}
