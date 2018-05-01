using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO.Ports;
using System.Threading;

namespace SerialComm
{
    public class CommBasic
    {
        private SerialPort serialPort;
        public bool Ready;
        private Thread connector;
        private bool connected = false;

        public CommBasic()
        {
            connector = new Thread(Connect);
            connector.IsBackground = true;
            connector.Start();
        }

        private void Connect()
        {
            while (true)
            {
                while (!connected)
                {
                    string[] ports = SerialPort.GetPortNames();
                    if (ports.Length > 0)
                    {
                        serialPort = new SerialPort();
                        serialPort.PortName = ports[0];
                        serialPort.BaudRate = 115200;
                        serialPort.Open();
                        serialPort.DiscardInBuffer();
                        serialPort.DataReceived += Sp_DataReceived;
                        connected = true;
                    }
                    else
                    {
                        Thread.Sleep(1);
                    }
                }
            }
        }

        private void Sp_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            string stuff = serialPort.ReadLine();
            Ready = true;
        }

        private void Write()
        {
            while (true)
            {
                Thread.Sleep(500);
                try
                {
                    serialPort.Write("S");
                }
                catch( Exception e)
                {
                    Console.WriteLine(e.Message);
                    if (serialPort.IsOpen)
                        serialPort.Close();
                }
            }
        }

        public void Step()
        {
            Ready = false;
            if (serialPort != null && serialPort.IsOpen)
                serialPort.Write("S");
        }

        public void Close()
        {
            connected = false;
            serialPort.Close();
            serialPort.Dispose();
        }
    }
}
