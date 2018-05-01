using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using SerialComm;

namespace Consumer
{
    public partial class Form1 : Form
    {
        SerialCommunicator sc;

        public Form1()
        {
            InitializeComponent();
            sc = new SerialCommunicator();
            List<SerialPortInfo> portInfos = sc.getPortNames();
            SerialPortInfo arduinoPort = portInfos.FirstOrDefault(p => p.Name.Contains("Arduino"));
            if (arduinoPort != null)
            {
                sc.connect(this, receive, arduinoPort.Name, 9600);
            }


        }

        private void getPortsButton_Click(object sender, EventArgs e)
        {
            Console.WriteLine(sc.getPortNames().Count());
            sc.getPortNames().ForEach(q => Console.WriteLine(q));
        }

        private void send(string message)
        {

        }

        private void receive(string message)
        {

        }
    }
}
