using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;

namespace KinectPrototype
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        public delegate void TemplateEvent(object sender, RoutedEventArgs e);
        public event TemplateEvent OpenClick;
        public event TemplateEvent DeleteClick;
        private void Open(object sender, RoutedEventArgs e)
        {
            OpenClick.Invoke(sender, e);
        }
        private void Delete(object sender, RoutedEventArgs e)
        {
            DeleteClick.Invoke(sender, e);
        }

    }
}
