using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.IO.Ports;
using System.Management;
using System.Diagnostics;
using System.Threading;
using System.ComponentModel;
using SerialComm;

namespace WPF
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    /// 

    public delegate void rxDelegate(string receive);

    //implement multiple concurrent serial comms
    public class SerialComm
    {
        //serial information struct
        public struct SerialPortInfo
        {
            public string DeviceID { get; set; }
            public string Name { get; set; }

            public override string ToString()
            {
                return Name;
            }

        }

        //the control
        private Control control;

        //the delegate
        private rxDelegate rxCallback;

        //the port
        private SerialPort serialPort;

        //status of the connection
        private bool connected { get { return serialPort != null && serialPort.IsOpen; } }

        //debug flag
        public bool debugEnabled = false;

        #region Public
        //Returns a list of all currently operational communication ports
        public List<SerialPortInfo> getPortNames()
        {
            List<SerialPortInfo> portNames = new List<SerialPortInfo>();
            try
            {
                ManagementObjectSearcher searcher = new ManagementObjectSearcher("root\\CIMV2", "SELECT * FROM Win32_SerialPort");
                foreach(ManagementObject queryObj in searcher.Get())
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

        //open a connection to the specified comport
        public bool connect(Control cont, rxDelegate callback, string portName, int baudRate)
        {
            //if it is not initialized, init it
            if(serialPort == null)
            {
                initialize(cont, callback, portName, baudRate);
            }
            else if(serialPort.IsOpen)
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
                ConsoleOutput(String.Format("Connected to port {0}", portName));
                return true;
            }
            catch
            {
                ConsoleOutput(String.Format("Unable to connect to port {0}", portName));
                return false;
            }
        }

        //disconnect the currently enabled serial communication
        public void disconnect()
        {
            if(serialPort != null && serialPort.IsOpen)
            {
                serialPort.Close();
                ConsoleOutput("Disconnected.");
            }
        }

        //send a serialized message
        public void send(string message)
        {
            if(serialPort != null && serialPort.IsOpen)
            {
                ConsoleOutput(String.Format("Sending Message: {0}", message));
                serialPort.WriteLine(message);
            }
            else
            {
                ConsoleOutput("Can not send message. Not connected to SerialPort.");
            }
        }

        //set the debug mode, default is false. will print to console if enabled
        public void setDebugMode(bool newValue)
        {
            debugEnabled = newValue;
        }
        #endregion

        #region Private

        //initialize a serial connection
        private void initialize(Control cont, rxDelegate callback, string portName, int baudRate)
        {
            serialPort = new SerialPort()
            {
                PortName = portName,
                BaudRate = baudRate,
                DtrEnable = true,
            };
            control = cont;
            rxCallback = callback;
            serialPort.DataReceived += SerialPort_DataReceived;
            ConsoleOutput("Initialized.");
        }

        //rx
        private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            string msg = serialPort.ReadLine();
            ConsoleOutput(String.Format("Reading Message: {0}", msg));

            control.Dispatcher.Invoke(rxCallback, msg);
        }


        //DEBUG
        private void ConsoleOutput(string message)
        {
            if(debugEnabled)
            {
                Console.WriteLine(String.Format("SerialCommunicator: {0}", message));
            }
        }

        #endregion


    }

    //this is the user
    public partial class MainWindow : Window
    {
        private SerialComm sc;

        public MainWindow()
        {
            InitializeComponent();

            //disconnect on close 
            Closed += MainWindow_Closed;

            //init our abstraction layer
            sc = new SerialComm();
            sc.debugEnabled = true;

            //grab the available ports
            ports = sc.getPortNames();

            //show the port names
            textBlock.Text = ports[0].ToString();

            //connect to first available port... change this to seek the port containing arduino
            sc.connect(this, new rxDelegate(rxFromArduino), ports[0].DeviceID, 115200);

            //on scan click, fire up a new thread
            Thread scanThread = new Thread(scanWorker);
            scanThread.IsBackground = true;
            scanThread.Start();



        }

        private void scanWorker()
        {
            //subscribe to events
            //ImageArrived

            
            //threadsafe exit

            
            while (true)
            {

            }

        }

        private void MainWindow_Closed(object sender, EventArgs e)
        {
            sc.disconnect();
        }

        //rx    
        public void rxFromArduino(string rx)
        {
            string data;
            //check transmission here
            if (rx[0].Equals(STX) && rx[rx.Length - 1].Equals(EOT))
            {
                //transmission is verifed, get the message
                data = rx.Split(STX, EOT)[1];
                if (data.Equals("OK"))
                {
                    //take a scan here
                    //Kinect.GetScan();
                }
                else
                {
                    //failure
                }
            }
        }

        //tx
        private void button_Click(object sender, RoutedEventArgs e)
        {
            //comm protocol:
            //STX : 2 on the ascii table +
            //MESSAGE +
            //EOT : 3 on the ascii table

            count++;

        }


    }
}
