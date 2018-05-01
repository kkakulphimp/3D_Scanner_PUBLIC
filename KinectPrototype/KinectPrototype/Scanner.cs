using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using KinectHelperLibrary;
using Microsoft.Xna.Framework;
using SerialComm;
using FileManagement;
using MathNet.Numerics.Statistics;
using System.IO;

namespace KinectPrototype
{
    public enum State
    {
        Scanning,
        Processing,
        Complete,
        Error
    }
    class Scanner
    {
        private Scanner ScannerContext;
        private KinectHelper m_kinectHelper;
        //private ScannerSerialCommunicator m_serialCommunicator;
        private volatile bool m_scanReady = false;
        private volatile bool m_Error = false;
        private Thread m_kinectThread;
        private BoundingBox m_scanVolume;
        private Vector3[] m_pointCloudBuffer;
        private int m_stepDelay;
        static CommBasic m_commBasic;
        private Vector3 m_centerPoint;
        public delegate void ScannerEvent(object sender, ScannerEventArgs e);
        public event ScannerEvent StatusUpdated;

        static Scanner()
        {
            m_commBasic = new CommBasic();
            }
        public Scanner(KinectHelper khelper, BoundingBox scanVolume, Vector3 centerPoint, int steps, int stepdelay)
        {

            m_centerPoint = centerPoint;
            m_stepDelay = stepdelay;
            ScannerContext = this;
            m_kinectHelper = khelper;
            //m_serialCommunicator = schelper;
            m_scanVolume = scanVolume;
            m_kinectThread = new Thread(Scan);
            m_kinectThread.IsBackground = true;
            m_kinectThread.Start(steps);
        }
        private void Scan(object sets)
        {
            //subscribe
            m_kinectHelper.FrameArrived += M_kinectHelper_ImagesArrived;

            if (sets is int && 200 % (int)sets == 0)
            {
                int scanNum = (int)sets;
                int stepsPerScan = 200 / scanNum;
                for (int i = 0; i < 200; i++)
                {
                    m_commBasic.Step();
                    Thread.Sleep(m_stepDelay);
                }
                Vector3[][] scans = new Vector3[scanNum][];
                for (int i = 0; i < scanNum; i++)
                {
                    if (!m_Error)
                    {
                        //frame here
                        m_kinectHelper.GetFrame(m_scanVolume);
                        while (!m_scanReady)
                            Thread.Sleep(1);
                        scans[i] = m_pointCloudBuffer;
                        m_scanReady = false;

                        //step here for as many steps as it needs
                        for (int j = 0; j < stepsPerScan; j++)
                        {
                            m_commBasic.Step();
                            while (!m_commBasic.Ready)
                                Thread.Sleep(1);
                            Thread.Sleep(m_stepDelay);
                        }
                        //Stabilization delay
                        Thread.Sleep(1000);
                        StatusUpdated.Invoke(ScannerContext, new ScannerEventArgs(
                            State.Scanning,
                            i / (float)scanNum,
                            m_pointCloudBuffer,
                            scans));
                    }
                    else
                    {
                        //terminate
                        StatusUpdated.Invoke(ScannerContext, new ScannerEventArgs(
                            State.Error,
                            i / (float)sets,
                            m_pointCloudBuffer,
                            scans));
                    }
                }
                StatusUpdated.Invoke(ScannerContext, new ScannerEventArgs(
                    State.Complete,
                    1f,
                    m_pointCloudBuffer,
                    scans));
            }
            else
                return;
        }

        private void M_kinectHelper_ImagesArrived(object sender, KinectFrameArgs e)
        {
            m_pointCloudBuffer = e.pointCloudV3;
            m_scanReady = true;
        }
    }
    class ScannerEventArgs : EventArgs
    {
        public State Status;
        public float Completion;
        public Vector3[][] PointCloud;
        public Vector3[] LastCloud;
        public ScannerEventArgs(State s, float completion, Vector3[] lpc, Vector3[][] pc)
        {
            Status = s;
            Completion = completion;
            PointCloud = pc;
            LastCloud = lpc;
        }
    }
}
