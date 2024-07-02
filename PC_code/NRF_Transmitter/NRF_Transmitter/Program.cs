using NRF_Transmitter;
using System.IO.Ports;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using System.Text.RegularExpressions;
using System.Text;

class Program
{
    private static readonly string myPictures = Environment.GetFolderPath(Environment.SpecialFolder.MyPictures);
    private static readonly string imgFilename = myPictures + "\\grayscale test images\\sunset.png";
    private static readonly string newFilename = myPictures + "\\grayscale test images\\changed.png";
    private static readonly string txtFilename = myPictures + "\\grayscale test images\\byteMap.txt";
    private static readonly string receivedImgFilename = myPictures + "\\grayscale test images\\received images\\received.png";
    private static readonly string receivedTxtFilename = myPictures + "\\grayscale test images\\received images\\byteMap.txt";

    private const string transmitterCOM = "COM23";
    private const string receiverCOM = "COM11";

    private const int baudRate = 1000000;
    private const int dataBits = 8;
    private const Parity parity = Parity.None;
    private const StopBits stopBits = StopBits.One;

    private const int flagBytesCount = 5;
    private const int inkplateFlagBytesCount = 5;
    private const int payloadSize = 30;
    private const byte bytesFlag = 0x01; // flag => [0] - 0x01, [1,..,4] - byte count
    private const byte bytesWakeFlag = 0x02; // flag => [0] - 0x02, [1,..,4] - byte count
    private const byte stringFlag = 0x03;
    private const byte ackFlag = 0xFF;
    private const byte nakFlag = 0x00;
    // IP (InkPlate) flags
    private const byte IPBytesFlag = 0x01; // flag => [0] - 0x01, [1,..,4] - byte count
    private const byte IPImageFlag = 0x02; // flag => [0] - 0x02, [1,2] - image heightAsBytes, [3,4] - image widthAsBytes
    private const byte IPStringFlag = 0x03; // flag => [0] - 0x03, [1,...,4] - string length
    private const byte IPImage3BitFlag = 0x04; // flag => [0] - 0x02, [1,2] - image heightAsBytes, [3,4] - image widthAsBytes

    private static SerialPort transmitterPort = null!;
    private static SerialPort receiverPort = null!;

    private static LinkedList<int> acks = new LinkedList<int>(); // clear when starting transmission



    static void Main(string[] args)
    {
        byte[][] img = ConvertImageForInkplate(imgFilename);
        byte[] img3Bit = Convert3BitImageForInkplate(imgFilename);
        transmitterPort = OpenPort(transmitterCOM);
        transmitterPort.DataReceived += (sender, e) => ReadFromArduino();

        try
        {
            transmitterPort.Open();
            Console.WriteLine("Type something and press Enter to send it to Arduino:");

            //Thread receiveThread = new Thread(ReceiveData); // Thread for listening to the receiving arduino
            //receiveThread.Start();

            while (true)
            {
                string input = Console.ReadLine() ?? "";

                if (input.ToLower().Equals("sendimg"))
                {
                    var watch = System.Diagnostics.Stopwatch.StartNew();
                    SendImage(img);
                    var elapsedMs = watch.ElapsedMilliseconds;
                    Console.WriteLine($"Time taken to send image: {elapsedMs}ms");
                }
                else if (Regex.IsMatch(input, @"^\s*sendimg3\s*(cont)?\s*$", RegexOptions.IgnoreCase))
                {
                    if (input.Contains("cont")) // cont - continuous sending
                    {
                        while (true)
                        {
                            if (SendImage3Bit(img3Bit, MyImageExtensions.inkplateHeight, MyImageExtensions.inkplateWidth))
                                break;

                            Console.WriteLine("Trying again");
                            Thread.Sleep(1500);
                        }
                    }
                    else
                    {
                        SendImage3Bit(img3Bit, MyImageExtensions.inkplateHeight, MyImageExtensions.inkplateWidth);
                    }
                }
                else if (Regex.IsMatch(input, @"^\s*send \d+\s*(cont)?\s*$", RegexOptions.IgnoreCase)) // ex. send 5, or send   30...
                {
                    int byteCount = Convert.ToInt32(input.Trim().Split(" ")[1]);
                    byte[] data = GetTestBytes(byteCount);
                    Console.Write("Sent: ");
                    for (int i = 0; i < data.Length; i++)
                    {
                        Console.Write("0x" + data[i].ToString("X2") + " ");
                    }
                    Console.WriteLine();
                    if (input.Contains("cont")) // cont - continuous sending
                    {
                        while (true)
                        {
                            if (SendByteArray(data))
                                break;

                            Console.WriteLine("Trying again");
                            Thread.Sleep(1500);
                        }
                    }
                    else
                    {
                        SendByteArray(data);
                    }
                }
                /*else if (Regex.IsMatch(input, @"\s*send [\w\s]+"))
                {
                    input = input.Trim().Substring(new string("send ").Length);
                    SendString(input);
                }*/
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("Error: " + ex.Message);
        }
        finally
        {
            if (transmitterPort != null)
                transmitterPort.Close();
        }
    }



    static bool SendInitFlag(int byteCount, bool wakeFlag = false)
    {
        byte[] dataSize = BitConverter.GetBytes((Int32)byteCount);
        Array.Reverse(dataSize); // little endian

        // establish communication (send flag)
        byte[] flag = new byte[flagBytesCount];
        if (wakeFlag)
            flag[0] = bytesWakeFlag;
        else
            flag[0] = bytesFlag;
        flag[1] = dataSize[0];
        flag[2] = dataSize[1];
        flag[3] = dataSize[2];
        flag[4] = dataSize[3];


        transmitterPort.Write(flag, 0, flag.Length);
        var stopWatch = System.Diagnostics.Stopwatch.StartNew();
        while (acks.Count == 0)
        {
            if (stopWatch.ElapsedMilliseconds > 2000)
            {
                Console.WriteLine("No ack received");
                return false;
            }
        }
        if (acks.First() == byteCount)
        {
            acks.RemoveFirst();
            return true;
        }

        return false;
    }


    // return true if transmission was successful
    static bool SendByteArray(byte[] data, bool sendFlag = true)
    {
        if (data.Length > Math.Pow(2, 32))
            throw new ArgumentException("Size of byte array is too big");

        byte[] dataSize = BitConverter.GetBytes(data.Length);
        Array.Reverse(dataSize); // little endian

        // establish communication (send flag)
        if (SendInitFlag(inkplateFlagBytesCount, true) == false)
            return false;
        byte[] inkplateFlag = new byte[inkplateFlagBytesCount];
        inkplateFlag[0] = IPBytesFlag;
        inkplateFlag[1] = dataSize[0];
        inkplateFlag[2] = dataSize[1];
        inkplateFlag[3] = dataSize[2];
        inkplateFlag[4] = dataSize[3];

        if (sendFlag)
        {
            transmitterPort.Write(inkplateFlag, 0, inkplateFlag.Length);
            Thread.Sleep(5);
        }


        // start sending data
        acks.Clear();
        int payloadCount = 0;
        int count = data.Length;
        if (SendInitFlag(count) == false)
            return false;
        while (count > 0)
        {
            int bytesToSend = 0;
            if (count > payloadSize)
                bytesToSend = payloadSize;
            else
                bytesToSend = count;

            transmitterPort.Write(data, data.Length - count, bytesToSend);
            count -= bytesToSend;
            // wait for ack and check if it's correct
            while (acks.Count == 0) ;
            if (acks.First() == payloadCount)
            {
                acks.RemoveFirst();
                payloadCount++;
            }
            else if (acks.First() == -1)
            {
                return false;
            }
            else
            {
                throw new Exception($"Incorrect ack | Expected: {count}, Received: {acks.First()}");
            }
        }

        return true;
    }


    [Obsolete("Needs to be updated, use SendImage3Bit for now")]
    static void SendImage(byte[][] img)
    {
        int height = img.Length;
        int width = img[0].Length;
        byte[] heightAsBytes = BitConverter.GetBytes((ushort)height);
        byte[] widthAsBytes = BitConverter.GetBytes((ushort)width);

        // little endian
        Array.Reverse(heightAsBytes);
        Array.Reverse(widthAsBytes);

        // establish communication (send flag)
        byte[] flag = new byte[flagBytesCount];
        flag[0] = IPImageFlag;
        flag[1] = heightAsBytes[0];
        flag[2] = heightAsBytes[1];
        flag[3] = widthAsBytes[0];
        flag[4] = widthAsBytes[1];

        transmitterPort.Write(flag, 0, flag.Length);
        Thread.Sleep(5);

        // start sending data
        acks.Clear();
        for (int i = 0; i < height; i++) // for each row
        {
            // send the row
            int count = width;
            while (count > 0)
            {
                int bytesToSend = 0;
                if (count > payloadSize)
                    bytesToSend = payloadSize;
                else
                    bytesToSend = count;

                transmitterPort.Write(img[i], width - count, bytesToSend);
                count -= bytesToSend;

                // wait for ack and check if it's correct
                while (acks.Count == 0) ;
                if (acks.First() == count)
                {
                    acks.RemoveFirst();
                }
                else if (acks.First() == -1)
                {
                    return;
                }

                else
                {
                    throw new Exception($"Incorrect ack | Expected: {count}, Received: {acks.First()}");
                }
            }
        }
    }



    static bool SendImage3Bit(byte[] img, int height, int width)
    {
        byte[] heightAsBytes = BitConverter.GetBytes((ushort)height);
        byte[] widthAsBytes = BitConverter.GetBytes((ushort)width);

        // little endian
        Array.Reverse(heightAsBytes);
        Array.Reverse(widthAsBytes);


        // establish communication with receiving controller (send flag)
        if (SendInitFlag(inkplateFlagBytesCount, true) == false)
            return false;
        byte[] inkplateFlag = new byte[inkplateFlagBytesCount];
        inkplateFlag[0] = IPImage3BitFlag;
        inkplateFlag[1] = heightAsBytes[0];
        inkplateFlag[2] = heightAsBytes[1];
        inkplateFlag[3] = widthAsBytes[0];
        inkplateFlag[4] = widthAsBytes[1];

        transmitterPort.Write(inkplateFlag, 0, inkplateFlag.Length);
        Thread.Sleep(5);

        // start sending data
        acks.Clear();
        int payloadCount = 0;
        int count = img.Length;
        if (SendInitFlag(count) == false)
            return false;
        while (count > 0)
        {
            int bytesToSend = 0;
            if (count > payloadSize)
                bytesToSend = payloadSize;
            else
                bytesToSend = count;

            transmitterPort.Write(img, img.Length - count, bytesToSend);
            count -= bytesToSend;

            // wait for ack and check if it's correct
            while (acks.Count == 0) ;
            if (acks.First() == payloadCount)
            {
                acks.RemoveFirst();
                payloadCount++;
            }
            else if (acks.First() == -1)
            {
                return false;
            }
            else
            {
                throw new Exception($"Incorrect ack | Expected: {count}, Received: {acks.First()}");
            }
        }

        return true;
    }



    static void SendString(string toSend)
    {
        byte[] lengthAsBytes = BitConverter.GetBytes(toSend.Length);

        Array.Reverse(lengthAsBytes);

        byte[] flag = new byte[flagBytesCount];
        flag[0] = IPStringFlag;
        flag[1] = lengthAsBytes[0];
        flag[2] = lengthAsBytes[1];
        flag[3] = lengthAsBytes[2];
        flag[4] = lengthAsBytes[3];

        transmitterPort.Write(flag, 0, flag.Length);
        Thread.Sleep(5);

        byte[] bytesToSend = Encoding.UTF8.GetBytes(toSend);

        SendByteArray(bytesToSend, false);
    }



    static void ReceiveData()
    {
        try
        {
            receiverPort = OpenPort(receiverCOM);
            receiverPort.Open();
            while (true)
            {
                if (receiverPort.BytesToRead >= flagBytesCount)
                {
                    byte[] flag = new byte[flagBytesCount];
                    receiverPort.Read(flag, 0, flag.Length);

                    if (flag[0] == bytesFlag)
                    {
                        Console.ForegroundColor = ConsoleColor.Blue;
                        int byteCount = (flag[1] << 24) | (flag[2] << 16) | (flag[3] << 8) | flag[4];
                        Console.WriteLine("bytec: " + byteCount);
                        byte[] received = ReceiveBytes(byteCount);
                        Console.Write("Received: ");
                        for (int i = 0; i < received.Length; i++)
                        {
                            Console.Write("0x" + received[i].ToString("X2") + " ");
                        }
                        Console.WriteLine();
                        Console.ForegroundColor = ConsoleColor.White;
                    }
                    else if (flag[0] == IPImageFlag)
                    {
                        Console.ForegroundColor = ConsoleColor.Blue;
                        int height = (flag[1] << 8) | flag[2];
                        int width = (flag[3] << 8) | flag[4];
                        Console.WriteLine("Height: " + height);
                        Console.WriteLine("Width: " + width);
                        ReceiveImage(height, width);
                        Console.ForegroundColor = ConsoleColor.White;
                    }
                    else if (flag[0] == IPImage3BitFlag)
                    {
                        Console.ForegroundColor = ConsoleColor.Blue;
                        int height = (flag[1] << 8) | flag[2];
                        int width = (flag[3] << 8) | flag[4];
                        Console.WriteLine("Height: " + height);
                        Console.WriteLine("Width: " + width);
                        ReceiveImage3Bit(height, width);
                        Console.ForegroundColor = ConsoleColor.White;
                    }
                    else if (flag[0] == IPStringFlag)
                    {
                        Console.ForegroundColor = ConsoleColor.Blue;
                        int length = (flag[1] << 24) | (flag[2] << 16) | (flag[3] << 8) | flag[4];
                        ReceiveString(length);
                        Console.ForegroundColor = ConsoleColor.White;
                    }
                }

            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("Error receiving data: " + ex.Message);
        }
        finally
        {
            if (receiverPort != null)
                receiverPort.Close();
        }
    }



    static byte[] ReceiveBytes(int byteCount)
    {
        byte[] received = new byte[byteCount];
        int count = byteCount;
        while (count > 0)
        {
            int bytesToReceive = 0;
            // Take at most a *payloadSize* sized byte chunk
            if (count > payloadSize)
                bytesToReceive = payloadSize;
            else
                bytesToReceive = count;

            while (receiverPort.BytesToRead < bytesToReceive) ;

            receiverPort.Read(received, byteCount - count, bytesToReceive);

            count -= bytesToReceive;
        }

        return received;
    }



    static void ReceiveImage(int height, int width)
    {
        byte[][] img = new byte[height][];
        for (int i = 0; i < height; i++)
        {
            img[i] = new byte[width];
        }

        int percentage = 1;
        for (int i = 0; i < height; i++)
        {
            int count = width;

            Array.Copy(ReceiveBytes(count), 0, img[i], 0, count);

            if ((i + 1) * width >= width * height * percentage / 100)
            {
                // for small image sizes, one row might skip over 2 or more percentage points
                while ((i + 1) * width >= width * height * (percentage + 1) / 100) percentage++;

                Console.WriteLine($"{percentage++}%");
            }
        }

        Image<Rgba32> image = ImageHandler.LoadImage(img);
        Console.WriteLine("saving");
        image.Save(receivedImgFilename);
        image.WriteAsByteMatrixToTextFile(receivedTxtFilename);
        Console.WriteLine("saved");
    }



    static void ReceiveImage3Bit(int height, int width)
    {
        byte[] img = new byte[width * height / 2];

        int count = img.Length;
        int percentage = 1;
        while (count > 0)
        {
            int bytesToReceive = 0;
            // Take at most a *payloadSize* sized byte chunk
            if (count > payloadSize)
                bytesToReceive = payloadSize;
            else
                bytesToReceive = count;

            while (receiverPort.BytesToRead < bytesToReceive) ;

            receiverPort.Read(img, img.Length - count, bytesToReceive);

            count -= bytesToReceive;

            if (img.Length - count >= img.Length * percentage / 100)
            {
                Console.WriteLine($"{percentage++}%");
            }
        }

        Image<Rgba32> image = ImageHandler.ConvertBitmap3BitToRgba32(img, width, height);
        Console.WriteLine("saving");
        image.Save(receivedImgFilename);
        image.WriteAsByteMatrixToTextFile(receivedTxtFilename);
        Console.WriteLine("saved");
    }



    static void ReceiveString(int length)
    {
        byte[] stringData = new byte[length];

        stringData = ReceiveBytes(length);

        string received = Encoding.UTF8.GetString(stringData);

        Console.WriteLine("Received: " + received);
    }



    static SerialPort OpenPort(string portName)
    {
        SerialPort port = new SerialPort();
        port.PortName = portName;
        port.BaudRate = baudRate;
        port.DataBits = dataBits;
        port.Parity = parity;
        port.StopBits = stopBits;
        port.Handshake = Handshake.None;
        port.RtsEnable = true;

        return port;
    }



    static byte[][] ConvertImageForInkplate(string filename)
    {
        Image<Rgba32> image = Image.Load<Rgba32>(filename);
        image.ConvertToGrayscale();
        image.ResizeForInklpate();
        image.Save(newFilename);
        image.WriteAsByteMatrixToTextFile(txtFilename);

        return image.GetAsByteMatrix();
    }



    static byte[] Convert3BitImageForInkplate(string filename)
    {
        Image<Rgba32> image = Image.Load<Rgba32>(filename);
        image.ConvertToGrayscale();
        image.AddDither();
        image.ResizeForInklpate();
        image.Save(newFilename);
        image.WriteAsByteMatrixToTextFile(txtFilename);

        return image.ConvertToBitmap3bit();
    }



    static byte[] GetTestBytes(int byteCount)
    {
        byte[] result = new byte[byteCount];

        for (int i = 0; i < byteCount; i++)
        {
            result[i] = (byte)i;
        }

        return result;
    }



    static void ListAvaliableCOMPorts()
    {
        string[] portNames = SerialPort.GetPortNames();

        if (portNames.Length > 0)
        {
            Console.WriteLine("Available COM Ports:");
            foreach (string portName in portNames)
            {
                Console.WriteLine(portName);
            }
        }
        else
        {
            Console.WriteLine("No COM Ports found.");
        }
    }



    static void ReadFromArduino()
    {
        if (transmitterPort.BytesToRead >= flagBytesCount)
        {
            byte[] flag = new byte[flagBytesCount];
            transmitterPort.Read(flag, 0, flag.Length);

            if (flag[0] == stringFlag)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                string dataReceived = transmitterPort.ReadLine();
                Console.WriteLine("Transmitter: " + dataReceived);
                Console.ForegroundColor = ConsoleColor.White;
            }
            else if (flag[0] == ackFlag)
            {
                int payloadCount = (flag[1] << 24) | (flag[2] << 16) | (flag[3] << 8) | flag[4];
                acks.AddLast(payloadCount); // save ack in queue
            }
            else if (flag[0] == nakFlag)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("NAK received");
                Console.ForegroundColor = ConsoleColor.White;
                acks.AddLast(-1); // save nak in queue
            }
        }
    }
}