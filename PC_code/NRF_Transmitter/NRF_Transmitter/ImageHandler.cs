using SixLabors.ImageSharp.PixelFormats;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace NRF_Transmitter
{
    internal static class ImageHandler
    {
        public static Image<Rgba32> LoadImage(string filename)
        {
            return Image.Load<Rgba32>(filename);
        }



        public static Image<Rgba32> LoadImage(byte[][] image)
        {
            return ConvertByteMatrixToImage(image);
        }



        private static Image<Rgba32> ConvertByteMatrixToImage(byte[][] byteMatrix)
        {
            int height = byteMatrix.Length;
            int width = byteMatrix[0].Length;
            var restoredImage = new Image<Rgba32>(width, height);

            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    byte intensity = byteMatrix[y][x];

                    // Set the color of the pixel in the restored image
                    restoredImage[x, y] = new Rgba32(intensity, intensity, intensity);
                }
            }

            return restoredImage;
        }



        // bitmap3Bit - each byte contains 2 pixels, first 4 bits is one pixel, second 4 is another,
        // since each pixel is 3 bits, the last bit is ignored
        public static Image<Rgba32> ConvertBitmap3BitToRgba32(byte[] bitmap3Bit, int width, int height)
        {
            Image<Rgba32> image = new Image<Rgba32>(width, height);

            for (int i = 0; i < height; i++)
            {
                for (int j = 0; j < width; j++)
                {
                    if ((i * height + j) % 2 == 0)
                    {
                        byte pixel = (byte)(((bitmap3Bit[(i * width + j) / 2] >> 4) >> 1) & 0x0F);
                        byte intensity = (byte)(pixel * 255 / 7);

                        image[j, i] = new Rgba32(intensity, intensity, intensity);
                    }
                    else
                    {
                        byte pixel = (byte)((bitmap3Bit[(i * width + j) / 2] & 0x0F) >> 1);
                        byte intensity = (byte)(pixel * 255 / 7);

                        image[j, i] = new Rgba32(intensity, intensity, intensity);
                    }
                }
            }

            return image;
        }
    }
}
