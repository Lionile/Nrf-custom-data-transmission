using NRF_Transmitter;
using System.Collections;
using System.Data;
using System.IO.Enumeration;
using System.IO.Ports;
using SixLabors.ImageSharp;
using System.Text.RegularExpressions;

class Program
{
    private static readonly string myPictures = Environment.GetFolderPath(Environment.SpecialFolder.MyPictures);
    private static readonly string imgFilename = myPictures +  "\\grayscale test images\\test1.png";
    private static readonly string newFilename = myPictures + "\\grayscale test images\\changed1.png";
    private static readonly string txtFilename = myPictures + "\\grayscale test images\\test1.txt";
    private static readonly string receivedImgFilename = myPictures + "\\grayscale test images\\received images\\test1.png";
    private static readonly string receivedTxtFilename = myPictures + "\\grayscale test images\\received images\\test1.txt";

    private const string transmitterCOM = "COM11";
    private const string receiverCOM = "COM3";

    private const int baudRate = 1000000;
    private const int dataBits = 8;
    private const Parity parity = Parity.None;
    private const StopBits stopBits = StopBits.One;

    private const int flagBytesCount = 5;
    private const byte bytesFlag = 0x01; // flag => [0] - 0x01, [1,2] - byte count
    private const byte imageFlag = 0x02; // flag => [0] - 0x02, [1,2] - image heightAsBytes, [3,4] - image widthAsBytes
    private const byte stringFlag = 0x03; // flag => [0] - 0x03
    // after receiving string flag, read string until /n (ReadLine)
    private const byte image3BitFlag = 0x04; // flag => [0] - 0x02, [1,2] - image heightAsBytes, [3,4] - image widthAsBytes
    private const byte ackFlag = 0xFF; // flag => [0] - 0xFF, [1,2] - ack count (ex. for image - remaining row byte count)

    private static SerialPort transmitterPort = null!;
    private static SerialPort receiverPort = null!;

    private static LinkedList<int> acks = new LinkedList<int>(); // clear when starting transmission



    static void Main(string[] args)
    {
        Console.WriteLine(Environment.CurrentDirectory);
        byte[][] img = ConvertImageForInkplate(imgFilename);
        byte[] img3Bit = Convert3BitImageForInkplate(imgFilename);
        transmitterPort = OpenPort(transmitterCOM);
        
        try
        {
            transmitterPort.Open();
            Console.WriteLine("Type something and press Enter to send it to Arduino:");

            Thread readThread = new Thread(ReadFromArduino); // Thread for listening to the transmitting arduino
            readThread.Start();
            Thread receiveThread = new Thread(ReceiveData); // Thread for listening to the receiving arduino
            receiveThread.Start();

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
                else if (input.ToLower().Equals("sendimg3"))
                {
                    var watch = System.Diagnostics.Stopwatch.StartNew();
                    SendImage3Bit(img3Bit, MyImageExtensions.inkplateHeight, MyImageExtensions.inkplateWidth);
                    var elapsedMs = watch.ElapsedMilliseconds;
                    Console.WriteLine($"Time taken to send image: {elapsedMs}ms");
                }
                else if (Regex.IsMatch(input, @"send \d+", RegexOptions.IgnoreCase)) // ex. send 5, or send 30...
                {
                    int byteCount = Convert.ToInt32(input.Split(" ")[1]);
                    byte[] data = GetTestBytes(byteCount);
                    Console.Write("Sent: ");
                    for (int i = 0; i < data.Length; i++)
                    {
                        Console.Write("0x" + data[i].ToString("X2") + " ");
                    }
                    Console.WriteLine();
                    SendByteArray(data);
                }
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



    static void SendByteArray(byte[] data)
    {
        if (data.Length > Math.Pow(2, 32))
            throw new ArgumentException("Size of byte array is too big");

        byte[] dataSize = BitConverter.GetBytes(data.Length);
        Array.Reverse(dataSize); // little endian

        // establish communication (send flag)
        byte[] flag = new byte[flagBytesCount];
        flag[0] = bytesFlag;
        flag[1] = dataSize[0];
        flag[2] = dataSize[1];
        flag[3] = dataSize[2];
        flag[4] = dataSize[3];

        transmitterPort.Write(flag, 0, flag.Length);
        Thread.Sleep(5);

        // start sending data
        int count = data.Length;
        while (count > 0)
        {
            int bytesToSend = 0;
            if (count > 32)
                bytesToSend = 32;
            else
                bytesToSend = count;

            transmitterPort.Write(data, data.Length - count, bytesToSend);
            count -= bytesToSend;
            // wait for ack and check if it's correct
            while (acks.Count == 0) ;
            if (acks.First() == count)
            {
                acks.RemoveFirst();
            }
            else
            {
                throw new Exception($"Incorrect ack | Expected: {count}, Received: {acks.First()}");
            }
        }
    }



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
        flag[0] = imageFlag;
        flag[1] = heightAsBytes[0];
        flag[2] = heightAsBytes[1];
        flag[3] = widthAsBytes[0];
        flag[4] = widthAsBytes[1];

        transmitterPort.Write(flag, 0, flag.Length);
        Thread.Sleep(5);

        // start sending data
        for (int i = 0; i < height; i++) // for each row
        {
            // send the row
            Console.WriteLine("sending: " + i);
            int count = width;
            while (count > 0)
            {
                int bytesToSend = 0;
                if (count > 32)
                    bytesToSend = 32;
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
                else
                {
                    throw new Exception($"Incorrect ack | Expected: {count}, Received: {acks.First()}");
                }
            }
        }
    }



    static void SendImage3Bit(byte[] img, int height, int width)
    {
        byte[] heightAsBytes = BitConverter.GetBytes((ushort)height);
        byte[] widthAsBytes = BitConverter.GetBytes((ushort)width);

        // little endian
        Array.Reverse(heightAsBytes);
        Array.Reverse(widthAsBytes);

        // establish communication (send flag)
        byte[] flag = new byte[flagBytesCount];
        flag[0] = image3BitFlag;
        flag[1] = heightAsBytes[0];
        flag[2] = heightAsBytes[1];
        flag[3] = widthAsBytes[0];
        flag[4] = widthAsBytes[1];

        transmitterPort.Write(flag, 0, flag.Length);
        Thread.Sleep(5);

        // start sending data
        int count = img.Length;
        while (count > 0)
        {
            int bytesToSend = 0;
            if (count > 32)
                bytesToSend = 32;
            else
                bytesToSend = count;

            transmitterPort.Write(img, img.Length - count, bytesToSend);
            count -= bytesToSend;

            // wait for ack and check if it's correct
            while (acks.Count == 0) ;
            if (acks.First() == count)
            {
                acks.RemoveFirst();
            }
            else
            {
                throw new Exception($"Incorrect ack | Expected: {count}, Received: {acks.First()}");
            }
        }
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
                        ReceiveBytes(flag);
                        Console.ForegroundColor = ConsoleColor.White;
                    }
                    else if (flag[0] == imageFlag)
                    {
                        Console.ForegroundColor = ConsoleColor.Blue;
                        ReceiveImage(flag);
                        Console.ForegroundColor = ConsoleColor.White;
                    }
                    else if (flag[0] == image3BitFlag)
                    {
                        Console.ForegroundColor = ConsoleColor.Blue;
                        ReceiveImage3Bit(flag);
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



    static void ReceiveBytes(byte[] flag)
    {
        int byteCount = (flag[1] << 24) | (flag[2] << 16) | (flag[3] << 8) | flag[4];
        Console.WriteLine("bytec: " + byteCount);
        byte[] received = new byte[byteCount];
        int count = byteCount;
        while (count > 0)
        {
            int bytesToReceive = 0;
            // Take at most a 32 byte chunk
            if (count > 32)
                bytesToReceive = 32;
            else
                bytesToReceive = count;

            while (receiverPort.BytesToRead < bytesToReceive) ;

            receiverPort.Read(received, byteCount - count, bytesToReceive);

            count -= bytesToReceive;
        }
        Console.Write("Received: ");
        for (int i = 0; i < received.Length; i++)
        {
            Console.Write("0x" + received[i].ToString("X2") + " ");
        }
        Console.WriteLine();
    }



    static void ReceiveImage(byte[] flag)
    {
        int height = (flag[1] << 8) | flag[2];
        int width = (flag[3] << 8) | flag[4];
        Console.WriteLine("Height: " + height);
        Console.WriteLine("Width: " + width);
        byte[][] img = new byte[height][];
        for (int i = 0; i < height; i++)
        {
            img[i] = new byte[width];
        }

        for (int i = 0; i < height; i++)
        {
            int count = width;
            while (count > 0)
            {
                int bytesToReceive = 0;
                // Take at most a 32 byte chunk
                if (count > 32)
                    bytesToReceive = 32;
                else
                    bytesToReceive = count;

                while (receiverPort.BytesToRead < bytesToReceive) ;

                receiverPort.Read(img[i], width - count, bytesToReceive);

                count -= bytesToReceive;
            }
            if (i == height / 4) Console.WriteLine("25%");
            if (i == height / 2) Console.WriteLine("50%");
            if (i == height * 3 / 4) Console.WriteLine("75%");
            Console.WriteLine(i);
        }

        Image<Rgba32> image = ImageHandler.LoadImage(img);
        Console.WriteLine("saving");
        image.Save(receivedImgFilename);
        image.WriteAsByteMatrixToTextFile(receivedTxtFilename);
        Console.WriteLine("saved");
    }



    static void ReceiveImage3Bit(byte[] flag)
    {
        int height = (flag[1] << 8) | flag[2];
        int width = (flag[3] << 8) | flag[4];
        Console.WriteLine("Height: " + height);
        Console.WriteLine("Width: " + width);
        byte[] img = new byte[width * height / 2];

        int count = img.Length;
        int percentage = 1;
        while (count > 0)
        {
            int bytesToReceive = 0;
            // Take at most a 32 byte chunk
            if (count > 32)
                bytesToReceive = 32;
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



    static SerialPort OpenPort(string portName)
    {
        SerialPort port = new SerialPort();
        port.PortName = portName;
        port.BaudRate = baudRate;
        port.DataBits = dataBits;
        port.Parity = parity;
        port.StopBits = stopBits;

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
        try
        {
            while (true)
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
                        int ackCount = (flag[1] << 24) | (flag[2] << 16) | (flag[3] << 8) | flag[4];
                        acks.AddLast(ackCount); // save ack in queue
                    }
                }

            }
        }
        
        catch (Exception ex)
        {
            Console.WriteLine("Error reading from Arduino: " + ex.Message);
        }
    }
}