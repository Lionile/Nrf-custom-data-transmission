using SixLabors.ImageSharp.PixelFormats;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace NRF_Transmitter
{
    public static class MyImageExtensions
    {
        public static readonly int inkplateWidth = 800;
        public const int inkplateHeight = 600;



        public static void ConvertToGrayscale<TPixel>(this Image<TPixel> image) where TPixel : unmanaged, IPixel<TPixel>
        {
            image.Mutate(x => x.Grayscale());
        }



        public static void ResizeForInklpate<TPixel>(this Image<TPixel> image) where TPixel : unmanaged, IPixel<TPixel>
        {
            Resize(image, inkplateWidth, inkplateHeight);
        }



        public static byte[][] GetAsByteMatrix<TPixel>(this Image<TPixel> image) where TPixel : unmanaged, IPixel<TPixel>
        {
            int width = image.Width;
            int height = image.Height;

            byte[][] pixelMatrix = new byte[height][];
            for (int i = 0; i < height; i++)
            {
                pixelMatrix[i] = new byte[width];
            }

            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    Rgba32 pixelColor = new Rgba32();
                    image[x, y].ToRgba32(ref pixelColor);
                    byte grayscaleValue = (byte)((pixelColor.R + pixelColor.G + pixelColor.B) / 3);

                    pixelMatrix[y][x] = grayscaleValue;
                }
            }

            return pixelMatrix;
        }



        public static void Resize<TPixel>(this Image<TPixel> image, int width, int height) where TPixel : unmanaged, IPixel<TPixel>
        {
            image.Mutate(x => x.Resize(new ResizeOptions
            {
                Size = new Size(width, height),
                Mode = ResizeMode.Stretch
            }));
        }



        public static void AddDither<TPixel>(this Image<TPixel> image) where TPixel : unmanaged, IPixel<TPixel>
        {
            image.Mutate(x => x.Dither());
        }



        public static void WriteAsByteMatrixToTextFile<TPixel>(this Image<TPixel> image, string filePath) where TPixel : unmanaged, IPixel<TPixel>
        {
            byte[][] byteMatrix = image.GetAsByteMatrix();

            using (StreamWriter writer = new StreamWriter(filePath))
            {
                int height = byteMatrix.Length;
                int width = byteMatrix[0].Length;

                for (int y = 0; y < height; y++)
                {
                    writer.Write("[");
                    for (int x = 0; x < width; x++)
                    {
                        // Write the pixel value in hexadecimal format with "0x" prefix
                        writer.Write("0x" + byteMatrix[y][x].ToString("X2"));
                        if (x < width - 1)
                        {
                            writer.Write(", ");
                        }
                    }
                    writer.Write("]");
                    writer.WriteLine();
                }
            }
        }



        public static byte[] ConvertToBitmap3bit<TPixel>(this Image<TPixel> image) where TPixel : unmanaged, IPixel<TPixel>
        {
            byte[] bitmap3Bit = new byte[image.Width * image.Height / 2];

            for (int i = 0; i < image.Height; i++)
            {
                for (int j = 0; j < image.Width; j++)
                {
                    if ((i * image.Height + j) % 2 == 0)
                    {
                        Rgba32 color = new Rgba32();
                        image[j, i].ToRgba32(ref color);
                        byte grayscaleValue = (byte)(((color.R + color.G + color.B) / 3 * 7 + 254) / 255);
                        bitmap3Bit[(i * image.Width + j) / 2] = (byte)(((grayscaleValue & 0x0F) << 4) << 1);

                        //Console.WriteLine($"image[{j},{i}], value={grayscaleValue.ToString("X2")}, RGB/3={(color.R + color.G + color.B) / 3}*7={(color.R + color.G + color.B) / 3 * 7}");
                    }
                    else
                    {
                        Rgba32 color = new Rgba32();
                        image[j, i].ToRgba32(ref color);
                        byte grayscaleValue = (byte)(((color.R + color.G + color.B) / 3 * 7 + 254) / 255);
                        bitmap3Bit[(i * image.Width + j) / 2] = (byte)(((grayscaleValue & 0x0F) << 1) | (bitmap3Bit[(i * image.Width + j) / 2] & 0xF0));

                        //Console.WriteLine($"image[{j},{i}], value={grayscaleValue.ToString("X2")}, RGB/3={(color.R + color.G + color.B) / 3}*7={(color.R + color.G + color.B) / 3 * 7}");
                    }
                }
            }


            return bitmap3Bit;
        }
    }
}
