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
using SerialComm;


namespace WpfConsumer
{
    /// <summary>
    /// Interaction logic for MainWindow.xamle
    /// </summary>
    public partial class MainWindow : Window
    {
        SerialCommunicator sc;

        public MainWindow()
        {
            InitializeComponent();
            sc = new SerialCommunicator();
            List<SerialPortInfo> portInfos = sc.getPortNames();
            SerialPortInfo arduinoPort = portInfos.FirstOrDefault(p => p.Name.Contains("Arduino"));
            if (arduinoPort != null)
            {
                //sc.connect(this, receive, arduinoPort.Name, 9600);
            }
        }

        private void Button_Click(object sender, RoutedEventArgs e)
        {

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
