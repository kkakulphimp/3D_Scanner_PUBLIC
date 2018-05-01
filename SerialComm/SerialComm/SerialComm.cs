using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO.Ports;
using System.Management;
using System.Windows.Forms;
using System.Windows;
using System.Threading;

namespace SerialComm
{
    //Serial Communications Class
    public class SerialComm
    {
        //delegates
        public delegate void eventDelegate(string receive);

        public event eventDelegate A_SerialDataReceived;
        Thread reader;

        //structure
        public class SerialPortInfo
        {
            public string DeviceID { get; set; }
            public string Name { get; set; }

            public override string ToString()
            {
                return Name;
            }
        }

        //status of the connection
        public bool connected { get { return serialPort != null && serialPort.IsOpen; } }

        //debug flag
        private bool debugEnabled = false;

        //the port
        private SerialPort serialPort;

        //open a connection to the specified comport
        public bool connect(string portName, int baudRate)
        {
            //if it is not initialized, init it
            if (serialPort == null)
            {
                initialize(portName, baudRate);
            }
            else if (serialPort.IsOpen)
            {
                ConsoleOutput("Already connected.");
            }
            else
            {
                serialPort.PortName = portName;
            }

            try
            {
                serialPort.Open();
                serialPort.DiscardInBuffer();
                ConsoleOutput(String.Format("Connected to port {0}", portName));
                return true;
            }
            catch
            {
                ConsoleOutput(String.Format("Unable to connect to port {0}", portName));
                return false;
            }
        }

        private void SerialPort_ErrorReceived(object sender, SerialErrorReceivedEventArgs e)
        {
            Console.WriteLine("Error received");
        }

        //disconnect the currently enabled serial communication
        public void disconnect()
        {
            if (serialPort != null && serialPort.IsOpen)
            {
                serialPort.DiscardInBuffer();
                serialPort.DiscardOutBuffer();
                serialPort.Close();
                ConsoleOutput("Disconnected.");
            }
        }

        //send a serialized message
        public void send(string message)
        {
            if (serialPort != null && serialPort.IsOpen)
            {
                ConsoleOutput(String.Format("Sending Message: {0}", message));
                serialPort.Write(message); 
            }
            else
            {
                ConsoleOutput("Can not send message. Not connected to SerialPort.");
            }
        }

        //Returns a list of all currently operational communication ports
        public List<SerialPortInfo> getPortNames()
        {
            List<SerialPortInfo> portNames = new List<SerialPortInfo>();
            try
            {
                ManagementObjectSearcher searcher = new ManagementObjectSearcher("root\\CIMV2", "SELECT * FROM Win32_SerialPort");
                foreach (ManagementObject queryObj in searcher.Get())
                {
                    portNames.Add(new SerialPortInfo()
                    {
                        DeviceID = (string)queryObj["DeviceID"],
                        Name = (string)queryObj["Name"]
                    });
                }
            }
            catch
            {
                portNames = SerialPort.GetPortNames().Select(p => new SerialPortInfo() { DeviceID = p, Name = p }).ToList();
            }
            return portNames;
        }

        //set the debug mode, default is false. will print to console if enabled
        public void setDebugMode(bool newValue)
        {
            debugEnabled = newValue;
        }

        #region private functions
        private void initialize(string portName, int baudRate)
        {
            serialPort = new SerialPort()
            {
                PortName = portName,
                BaudRate = baudRate,
                DtrEnable = true,
            };

            serialPort.DataReceived += SerialPort_DataReceived;
            serialPort.ErrorReceived += SerialPort_ErrorReceived;

            ConsoleOutput("Initialized.");
        }

        //rx
        private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            string msg = serialPort.ReadLine();
            ConsoleOutput(String.Format("Reading Message: {0}", msg));

            A_SerialDataReceived.Invoke(msg);
        }

        private void ConsoleOutput(string message)
        {
            if (debugEnabled)
            {
                Console.WriteLine(String.Format("SerialCommunicator: {0}", message));
            }
        }
        #endregion

    }

    //Business logic layer -- thin wrapper for the communicator for scanning application
    public class ScannerSerialCommunicator
    {
        //init our abstraction layer
        private SerialComm sc;

        //state of motor
        public enum StepState
        {
            OK,
            ERROR
        }

        //delegates
        public delegate void eventDelegate(StepState arg);

        public event eventDelegate BL_Good_SerialDataReceived;
        public event eventDelegate BL_Bad_SerialDataReceived;


        private const char STX = '\u0002';
        private const char EOT = '\u0004';

        private string rxData;
        private List<SerialComm.SerialPortInfo> ports;

        //constructor
        //-fires up a thin wrapper for the serial communicator abstraction layer
        public ScannerSerialCommunicator()
        {
            sc = new SerialComm();
            sc.setDebugMode(true);
            
            sc.A_SerialDataReceived += Sc_A_SerialDataReceived;
        }

        public void connect()
        {
            //grab the available ports
            ports = sc.getPortNames();

            //connect to arduino port
            sc.connect(ports.Find(q => q.Name.Contains("Arduino")).DeviceID, 115200);
        }

        //format the send data
        public void send(string msg)
        {
            sc.send(STX + "STEP" + EOT);
        }

        public void disconnect()
        {
            sc.disconnect();
        }

        private void Sc_A_SerialDataReceived(string rx)
        {
            string data = null;

            //check transmission
            if (rx[0].Equals(STX) && rx[rx.Length - 1].Equals(EOT))
            {
                //transmission is verifed, get the message
                data = rx.Split(STX, EOT)[1];

                if (data.Equals("OK"))
                {
                    BL_Good_SerialDataReceived.Invoke(StepState.OK);
                }
                else
                {
                    //handle errors here
                    BL_Bad_SerialDataReceived.Invoke(StepState.ERROR);
                }
            }
        }
    }
}

