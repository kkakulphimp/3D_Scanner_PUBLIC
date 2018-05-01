using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Kinect;
using System.Threading;
using System.Windows.Media.Imaging;
using System.Windows.Media;
using Microsoft.Xna.Framework;
using System.Windows;
using MathNet.Numerics.Statistics;

namespace KinectHelperLibrary
{
    public enum FrameType
    {
        Alignment,
        Calibration,
        Full
    }
    public class KinectHelper
    {
        private KinectHelper HelperContext;
        private KinectSensor m_kinectSensor;
        private CoordinateMapper m_coordinateMapper;
        private MultiSourceFrameReader m_multiSourceFrameReader;
        private FrameDescription m_colorFrameDescription;
        private FrameDescription m_infraredFrameDescription;
        private FrameDescription m_depthFrameDescription;
        private byte[] m_colorPixels;
        private ushort[] m_depthData;
        private byte[] m_depthPixels;
        private ushort[] m_infraredData;
        private byte[] m_infraredPixels;
        private CameraSpacePoint[] m_cameraSpacePoints;
        private const int c_bytesPerPixel = 4;
        private WriteableBitmap m_colorImage;
        private WriteableBitmap m_infraredImage;
        private WriteableBitmap m_depthImage;
        private const float c_InfraredSourceValueMaximum = ushort.MaxValue;
        private const float c_InfraredOutputValueMinimum = 0.01f;
        private const float c_InfraredOutputValueMaximum = 1.0f;
        private const float c_InfraredSceneValueAverage = 0.08f;
        private const float c_InfraredSceneStandardDeviations = 3.0f;
        private Vector3 m_alignmentAxis = new Vector3(0, 1, 0.6f);
        public bool Connected { get; private set; }
        private BoundingBox m_rawBounds = new BoundingBox(new Vector3(-1, -1, -1), new Vector3(1, 1, 1));
        public delegate void KinectFrameEvent(object sender, KinectFrameArgs e);
        public event KinectFrameEvent FrameArrived;
        private float m_noisetolerance = 1;
        public int m_samplingRate = 5;
        private Tuple<ushort, double>[] m_pixelStats;
        private ushort m_minDepth;
        private ushort m_maxDepth;
        private int m_sampleIndex;
        private int m_nullPixelsAllowed;

        private static bool s_libraryOpen = false;
        private Thread m_sensorLoop;
        private bool m_frameBool;
        private int m_alignmm;
        private FrameType m_currentFrameType;

        private ushort[][] m_tempDepthData;


        static KinectHelper()
        {
            s_libraryOpen = true;
        }
        public KinectHelper()
        {
            HelperContext = this;
            m_sensorLoop = new Thread(ConnectionMonitor);
            m_sensorLoop.IsBackground = true;
            m_sensorLoop.Start();
        }
        private void ConnectionMonitor()
        {
            while (s_libraryOpen)
            {
                //Waiting until kinect sensor is present
                if (m_kinectSensor is null)
                {
                    //Get sensor
                    m_kinectSensor = KinectSensor.GetDefault();
                    m_coordinateMapper = m_kinectSensor.CoordinateMapper;
                    m_multiSourceFrameReader = m_kinectSensor.OpenMultiSourceFrameReader(FrameSourceTypes.Color | FrameSourceTypes.Depth | FrameSourceTypes.Infrared);
                    m_multiSourceFrameReader.MultiSourceFrameArrived += MultiSourceFrameArrived;
                    m_kinectSensor.Open();
                    m_depthFrameDescription = m_kinectSensor.DepthFrameSource.FrameDescription;
                    m_infraredFrameDescription = m_kinectSensor.InfraredFrameSource.FrameDescription;
                    m_colorFrameDescription = m_kinectSensor.ColorFrameSource.FrameDescription;

                    //Frame size info
                    int depthFrameSize = m_depthFrameDescription.Width * m_depthFrameDescription.Height;
                    int infraredFrameSize = m_infraredFrameDescription.Width * m_infraredFrameDescription.Height;
                    int colorFrameSize = m_colorFrameDescription.Width * m_colorFrameDescription.Height;

                    //Instantiate with size info
                    m_depthData = new ushort[depthFrameSize];
                    m_infraredData = new ushort[infraredFrameSize];
                    m_colorPixels = new byte[colorFrameSize * c_bytesPerPixel];
                    m_depthPixels = new byte[depthFrameSize * c_bytesPerPixel];
                    m_infraredPixels = new byte[infraredFrameSize * c_bytesPerPixel];
                    m_cameraSpacePoints = new CameraSpacePoint[depthFrameSize];
                    Connected = true;
                }
                else
                    Thread.Sleep(1);

            }
        }
        private void MultiSourceFrameArrived(object sender, MultiSourceFrameArrivedEventArgs e)
        {
            if (m_frameBool)
            {
                //pull multisource frame reference out
                MultiSourceFrame multiSourceFrame = e.FrameReference.AcquireFrame();
                //Return on null
                if (multiSourceFrame is null)
                {
                    return;
                }
                //Calibration and full get sampled number of frames
                if ((m_currentFrameType.Equals(FrameType.Calibration) || m_currentFrameType.Equals(FrameType.Full)))
                {
                    using (DepthFrame depthFrame = multiSourceFrame.DepthFrameReference.AcquireFrame())
                    {
                        //Store one frame 
                        m_tempDepthData[m_sampleIndex] = new ushort[m_depthFrameDescription.Width * m_depthFrameDescription.Height];
                        depthFrame.CopyFrameDataToArray(m_tempDepthData[m_sampleIndex]);
                        m_minDepth = depthFrame.DepthMinReliableDistance;
                        m_maxDepth = depthFrame.DepthMaxReliableDistance;
                    }
                    //...until all samples are acquired
                    if (m_sampleIndex == m_samplingRate - 1)
                    {
                        //Then clean the points
                        CleanDepth();
                    }
                    else
                    {
                        //Not done, get next sample
                        m_sampleIndex++;
                        return;
                    }
                }
                //Instantiate images
                m_depthImage = new WriteableBitmap(m_depthFrameDescription.Width, m_depthFrameDescription.Height, 96, 96, PixelFormats.Bgr32, null);
                m_colorImage = new WriteableBitmap(m_colorFrameDescription.Width, m_colorFrameDescription.Height, 96, 96, PixelFormats.Bgr32, null);
                m_infraredImage = new WriteableBitmap(m_infraredFrameDescription.Width, m_infraredFrameDescription.Height, 96, 96, PixelFormats.Bgr32, null);
                switch (m_currentFrameType)
                {
                    case FrameType.Alignment:
                        using (DepthFrame depthframe = multiSourceFrame.DepthFrameReference.AcquireFrame())
                        {
                            depthframe.CopyFrameDataToArray(m_depthData);
                            m_maxDepth = depthframe.DepthMaxReliableDistance;
                            m_minDepth = depthframe.DepthMinReliableDistance;
                            ProcessDepth();
                            KinectFrameArgs args = new KinectFrameArgs(FrameType.Alignment, m_depthImage);
                            args.pointCloudV3 = m_cameraSpacePoints.Where(x => x.X != float.NegativeInfinity).Select(x => new Vector3(x.X, x.Y, x.Z)).ToArray();
                            FrameArrived.Invoke(HelperContext, args);
                        }
                        break;
                    case FrameType.Calibration:
                        ProcessDepth();
                        FrameArrived.Invoke(HelperContext, new KinectFrameArgs(FrameType.Calibration, m_depthImage));
                        break;
                    case FrameType.Full:
                        using (ColorFrame colorFrame = multiSourceFrame.ColorFrameReference.AcquireFrame())
                        using (InfraredFrame infraredFrame = multiSourceFrame.InfraredFrameReference.AcquireFrame())
                        {
                            ProcessDepth();
                            ProcessColor(colorFrame);
                            ProcessInfrared(infraredFrame);
                            KinectFrameArgs args = new KinectFrameArgs(FrameType.Full, m_depthImage, m_colorImage, m_infraredImage);
                            args.pointCloudCSP = m_cameraSpacePoints;
                            args.pointCloudV3 = m_cameraSpacePoints.Where(x => x.X != float.NegativeInfinity).Select(x => new Vector3(x.X, x.Y, x.Z)).ToArray();
                            FrameArrived.Invoke(HelperContext, args);
                        }
                        break;
                }
                m_frameBool = false;
            }
            else
                return;
        }
        private void ProcessColor(ColorFrame colorFrame)
        {
            colorFrame.CopyConvertedFrameDataToArray(m_colorPixels, ColorImageFormat.Bgra);
            m_colorImage = RenderPixelArray(colorFrame.FrameDescription, m_colorPixels);
        }
        private void ProcessDepth()
        {
            m_coordinateMapper.MapDepthFrameToCameraSpace(m_depthData, m_cameraSpacePoints);
            //Remove all ignores
            DepthBound();
            ConvertDepthDataToPixels();
            m_depthImage = RenderPixelArray(m_depthFrameDescription, m_depthPixels);
        }
        private void CleanDepth()
        {
            //Set up statistics collection
            m_pixelStats = new Tuple<ushort, double>[m_depthData.Length];
            double maxStdDev = 0;
            //iterate for every pixel
            for (int i = 0; i < m_depthData.Length; i++)
            {
                //set up temporary collection for all of the pixel's values
                ushort[] pixelData = new ushort[m_tempDepthData.Length];
                for (int j = 0; j < m_tempDepthData.Length; j++)
                {
                    pixelData[j] = m_tempDepthData[j][i];
                }
                var goodValues = pixelData.Where(x => !x.Equals(0));
                //int zeroes = pixelData.Where(x => x.Equals(0)).Count();
                if (pixelData.Length - goodValues.Count() <= m_nullPixelsAllowed && goodValues.Count() > 0)
                {
                    m_pixelStats[i] = new Tuple<ushort, double>((ushort)goodValues.Average(x => x), CalculateStdDev(goodValues));
                }
                else
                {
                    m_pixelStats[i] = new Tuple<ushort, double>(0, 0);
                }

            }
            maxStdDev = m_pixelStats.Max(x => x.Item2);
            //compare each pixel's standard deviation to the max standard deviation
            for (int i = 0; i < m_depthData.Length; i++)
            {
                if (m_pixelStats[i].Item2 / maxStdDev < m_noisetolerance)
                    m_depthData[i] = m_pixelStats[i].Item1;
                else
                    m_depthData[i] = 0;
            }

        }
        private void ProcessInfrared(InfraredFrame infraredFrame)
        {
            infraredFrame.CopyFrameDataToArray(m_infraredData);
            ConvertInfraredDataToPixels();
            m_infraredImage = RenderPixelArray(infraredFrame.FrameDescription, m_infraredPixels);
        }
        private void DepthBound()
        {
            for (int i = 0; i < m_cameraSpacePoints.Length; i++)
            {
                if (!m_rawBounds.Contains(new Vector3(m_cameraSpacePoints[i].X, m_cameraSpacePoints[i].Y, m_cameraSpacePoints[i].Z)).Equals(ContainmentType.Contains))
                {
                    m_cameraSpacePoints[i].X = float.NegativeInfinity;
                    m_cameraSpacePoints[i].Y = float.NegativeInfinity;
                    m_cameraSpacePoints[i].Z = float.NegativeInfinity;
                    m_depthData[i] = 0;
                }
            }
        }
        private void ConvertInfraredDataToPixels()
        {
            //convert the infrared to RGB
            int colorPixelIndex = 0;
            for (int i = 0; i < m_infraredData.Length; i++)
            {
                // normalize the incoming infrared data (ushort) to 
                // a float ranging from InfraredOutputValueMinimum
                // to InfraredOutputValueMaximum] by

                // 1. dividing the incoming value by the 
                // source maximum value
                float intensityRatio = m_infraredData[i] / c_InfraredSourceValueMaximum;

                // 2. dividing by the 
                // (average scene value * standard deviations)
                intensityRatio /=
                 c_InfraredSceneValueAverage * c_InfraredSceneStandardDeviations;

                // 3. limiting the value to InfraredOutputValueMaximum
                intensityRatio = Math.Min(c_InfraredOutputValueMaximum,
                    intensityRatio);

                // 4. limiting the lower value InfraredOutputValueMinimum
                intensityRatio = Math.Max(c_InfraredOutputValueMinimum,
                    intensityRatio);

                // 5. converting the normalized value to a byte and using 
                // the result as the RGB components required by the image
                byte intensity = (byte)(intensityRatio * 255.0f);
                m_infraredPixels[colorPixelIndex++] = intensity; //Blue
                m_infraredPixels[colorPixelIndex++] = intensity; //Green
                m_infraredPixels[colorPixelIndex++] = intensity; //Red
                m_infraredPixels[colorPixelIndex++] = 255;       //Alpha 
            }
        }

        private void ConvertDepthDataToPixels()
        {
            int colorPixelIndex = 0;
            // Shape the depth to the range of a byte
            int mapDepthToByte = m_maxDepth / 256;

            for (int i = 0; i < m_depthData.Length; ++i)
            {
                // Get the depth for this pixel
                ushort depth = m_depthData[i];

                // To convert to a byte, we're mapping the depth value
                // to the byte range.
                // Values outside the reliable depth range are 
                // mapped to 0 (black).
                byte intensity = (byte)(depth >= m_minDepth &&
                    depth <= m_maxDepth ? (depth / mapDepthToByte) : 0);
                if (m_currentFrameType.Equals(FrameType.Alignment))
                {
                    float x = m_cameraSpacePoints[i].X;
                    float z = m_cameraSpacePoints[i].Z;
                    if (x >= m_alignmentAxis.X - m_alignmm / 1000f &&
                        x <= m_alignmentAxis.X + m_alignmm / 1000f &&
                        z >= m_alignmentAxis.Z - m_alignmm / 1000f &&
                        z <= m_alignmentAxis.Z + m_alignmm / 1000f
                        )
                    {
                        m_depthPixels[colorPixelIndex++] = 0; //Blue
                        m_depthPixels[colorPixelIndex++] = 0; //Green
                        m_depthPixels[colorPixelIndex++] = 255; //Red
                        m_depthPixels[colorPixelIndex++] = 255; //Alpha
                    }
                    else
                    {
                        m_depthPixels[colorPixelIndex++] = intensity; //Blue
                        m_depthPixels[colorPixelIndex++] = intensity; //Green
                        m_depthPixels[colorPixelIndex++] = intensity; //Red
                        m_depthPixels[colorPixelIndex++] = 255; //Alpha
                    }
                }
                else
                {
                    m_depthPixels[colorPixelIndex++] = intensity; //Blue
                    m_depthPixels[colorPixelIndex++] = intensity; //Green
                    m_depthPixels[colorPixelIndex++] = intensity; //Red
                    m_depthPixels[colorPixelIndex++] = 255; //Alpha
                }
            }
        }

        private WriteableBitmap RenderPixelArray(FrameDescription frame, byte[] pixels)
        {
            WriteableBitmap bitmap;
            bitmap = new WriteableBitmap(frame.Width, frame.Height, 96, 96, PixelFormats.Bgr32, null);
            Int32Rect rect = new Int32Rect(0, 0, frame.Width, frame.Height);
            int stride = 4 * bitmap.PixelWidth;
            bitmap.WritePixels(rect, pixels, stride, 0);
            bitmap.Freeze();
            return bitmap;
        }
        public void Close()
        {
            s_libraryOpen = false;
        }
        public void GetFrame(BoundingBox box)
        {
            if (!m_frameBool)
            {
                m_sampleIndex = 0;
                m_tempDepthData = new ushort[m_samplingRate][];
                m_rawBounds = box;
                m_currentFrameType = FrameType.Full;
                m_frameBool = true;
            }
        }
        public void GetAlignmentFrame(BoundingBox box, Vector3 alignment, int mm)
        {
            if (!m_frameBool)
            {
                m_rawBounds = box;
                m_alignmentAxis = alignment;
                m_alignmm = mm;
                m_currentFrameType = FrameType.Alignment;
                m_frameBool = true;
            }
        }
        public void GetCalibrationFrame(BoundingBox box, int samples, float tolerance, int nulls)
        {
            if (!m_frameBool)
            {
                m_nullPixelsAllowed = nulls;
                m_sampleIndex = 0;
                m_samplingRate = samples;
                m_tempDepthData = new ushort[m_samplingRate][];
                m_rawBounds = box;
                m_noisetolerance = tolerance;
                m_currentFrameType = FrameType.Calibration;
                m_frameBool = true;
            }

        }
        private double CalculateStdDev(IEnumerable<ushort> values)
        {
            double ret = 0;
            if (values.Count() > 0)
            {
                //Compute the Average      
                double avg = values.Average(x => x);
                //Perform the Sum of (value-avg)_2_2      
                double sum = values.Sum(d => Math.Pow(d - avg, 2));
                //Put it all together      
                ret = Math.Sqrt((sum) / (values.Count() - 1));
            }
            return ret;
        }
    }
    public class KinectFrameArgs : EventArgs
    {
        public WriteableBitmap ColorImage
        {
            get
            {
                return cImage?.Clone();
            }
        }
        public WriteableBitmap DepthImage
        {
            get
            {
                return dImage?.Clone();
            }
        }
        public WriteableBitmap InfraredImage
        {
            get
            {
                return irImage?.Clone();
            }
        }
        private WriteableBitmap cImage;
        private WriteableBitmap dImage;
        private WriteableBitmap irImage;
        public Vector3[] pointCloudV3;
        public CameraSpacePoint[] pointCloudCSP;
        public FrameType TypeArrived;
        public double[] PixelStats;
        public KinectFrameArgs(FrameType frt, WriteableBitmap depth = null, WriteableBitmap color = null, WriteableBitmap ir = null)
        {
            cImage = color;
            dImage = depth;
            irImage = ir;
            TypeArrived = frt;
        }
    }
}
