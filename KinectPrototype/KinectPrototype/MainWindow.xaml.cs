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
using System.Threading;
using System.Windows.Threading;
using KinectHelperLibrary;
using Microsoft.Xna.Framework;
using System.Runtime.Serialization.Formatters.Binary;
using SerialComm;
using System.Diagnostics;
using FileManagement;
using System.IO;
using Microsoft.Win32;
using LiveCharts.Wpf;
using MathNet.Numerics.Statistics;
using System.Windows.Media.Media3D;
using Microsoft.DirectX.Direct3D;
using System.DirectoryServices.ActiveDirectory;
using System.Collections.ObjectModel;

namespace KinectPrototype
{
    public enum ViewTabs
    {
        Scan,
        Calibrate,
        Align,
        Processing
    }
    public struct Stats
    {
        public int TotalPoints;
        public int ReducedPoints;
        public double Average;
        public double StandardDeviation;
        public double OctalNodes;
        public Vector3 OctalDim;
    }
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private DispatcherTimer m_timer = new DispatcherTimer();
        //private ScannerSerialCommunicator m_serialCommunicator; //define outside
        private KinectHelper m_kinectHelper = new KinectHelper();
        private BoundingBox m_boundingContext = new BoundingBox(new Vector3(-1, -1, 0), new Vector3(1, 1, 1));
        private ViewTabs m_currentView = ViewTabs.Scan;
        public WriteableBitmap m_colorImage;
        public WriteableBitmap m_infraredImage;
        public WriteableBitmap m_depthImage;
        private Vector3 m_rotationAxis;
        private Scanner m_scanner;
        private Vector3[][] m_lastPointCloud;
        private Stopwatch m_stopwatch;
        private long m_lastPoll = 0;
        private bool m_autoCal = false;
        private BinaryFormatter m_binaryFormatter = new BinaryFormatter();
        private int m_lastScanNum;
        private BoundingBox m_lastBoundingContext;
        private Vector3 m_lastRotationAxis;
        private MeshGeometry3D m_lastMesh;
        private Stats m_lastStats;
        private GeometryModel3D m_geometryModel3D = new GeometryModel3D();
        private FileSystemWatcher m_fileSystemWatcher;
        private Model3DGroup m_model3DGroup = new Model3DGroup();
        private ModelVisual3D m_modelVisual3D = new ModelVisual3D();
        private DispatcherTimer m_rotationTimer = new DispatcherTimer();
        private List<Project> m_projects = new List<Project>();
        private List<WriteableBitmap> m_images = new List<WriteableBitmap>();

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            //C: \Users\karun\Documents\GitHub\cmpe2960_project_files\KinectPrototype\KinectPrototype\bin\Debug\processing
            //Load settings from previous instance
            if (File.Exists("settings.bin"))
            {
                FileStream fs = new FileStream("settings.bin", FileMode.Open);
                object thing = m_binaryFormatter.Deserialize(fs) as StateSaver;
                if (thing is StateSaver)
                {
                    StateSaver state = thing as StateSaver;
                    Al_xSlider.Value = state.Al.X;
                    Al_zSlider.Value = state.Al.Z;
                    Cal_xminSlider.Value = state.CalMin.X;
                    Cal_yminSlider.Value = state.CalMin.Y;
                    Cal_zminSlider.Value = state.CalMin.Z;
                    Cal_xmaxSlider.Value = state.CalMax.X;
                    Cal_ymaxSlider.Value = state.CalMax.Y;
                    Cal_zmaxSlider.Value = state.CalMax.Z;
                    Cal_sampleSlider.Value = state.CalSam;
                    Cal_varianceSlider.Value = state.CalVar;
                    Cal_nullPixSlider.Value = state.CalNull;
                }
            }
            if (Directory.Exists(Directory.GetCurrentDirectory() + @"\projects\"))
            {
                string[] files = Directory.GetFiles(Directory.GetCurrentDirectory() + @"\projects\");
                foreach (string item in files)
                {
                    FileStream fs = new FileStream(item, FileMode.Open);
                    if (item.Contains(".png"))
                    {
                        BitmapImage bmi = new BitmapImage(new Uri(item, UriKind.Absolute));
                        WriteableBitmap bitmap = new WriteableBitmap(bmi);
                        m_images.Add(bitmap);
                    }
                    else
                    {
                        object stuff = m_binaryFormatter.Deserialize(fs);
                        if (stuff is Project)
                            m_projects.Add(stuff as Project);
                    }
                }
            }
            //Set timer for frame polling
            m_timer.Interval = new TimeSpan(0, 0, 0, 0, 1);
            m_timer.Tick += M_timer_Tick;
            m_kinectHelper.FrameArrived += M_kinectHelper_ImagesArrived;
            //Stopwatch for the polling statistics
            m_stopwatch = new Stopwatch();
            m_stopwatch.Start();
            //Rotation timer
            m_rotationTimer.Interval = new TimeSpan(0, 0, 0, 0, 50);
            m_rotationTimer.Tick += M_rotationTimer_Tick;
            m_rotationTimer.Start();
            //Make watcher
            m_fileSystemWatcher = new FileSystemWatcher(Directory.GetCurrentDirectory() + @"\processing\", "*.m");
            m_fileSystemWatcher.EnableRaisingEvents = true;
            m_fileSystemWatcher.IncludeSubdirectories = true;
            m_fileSystemWatcher.Created += M_fileSystemWatcher_Created;

            for (int i = 0; i < m_projects.Count; i++)
            {
                Pro_projects.Items.Add(m_projects[i]);
            }
            //Run an initial calibration and align based on settings load

            m_rotationAxis = new Vector3((float)Al_xSlider.Value, 1, (float)Al_zSlider.Value);
            m_kinectHelper.GetAlignmentFrame(m_boundingContext, m_rotationAxis, (int)Al_mmSlider.Value);
            float xmin = (float)Cal_xminSlider.Value;
            float xmax = (float)Cal_xmaxSlider.Value;
            float ymin = (float)Cal_yminSlider.Value;
            float ymax = (float)Cal_ymaxSlider.Value;
            float zmin = (float)Cal_zminSlider.Value;
            float zmax = (float)Cal_zmaxSlider.Value;
            m_boundingContext = new BoundingBox(new Vector3(xmin, ymin, zmin), new Vector3(xmax, ymax, zmax));
            m_kinectHelper.GetCalibrationFrame(m_boundingContext, (int)Cal_sampleSlider.Value, (float)Cal_varianceSlider.Value, (int)Cal_nullPixSlider.Value);

        }

        private void App_DeleteClick(object sender, RoutedEventArgs e)
        {
            MessageBox.Show($"{(VisualTreeHelper.GetParent(VisualTreeHelper.GetParent(sender as Button)))}");
        }

        private void App_OpenClick(object sender, RoutedEventArgs e)
        {
            MessageBox.Show($"{VisualTreeHelper.GetParent(sender as Button)}");
        }

        private void M_rotationTimer_Tick(object sender, EventArgs e)
        {
            if (!(m_lastMesh is null))
            {
                //Render3D();
            }
        }

        public MainWindow()
        {
            //WPF can freaks out if there's certain things before the window exists
            //Use the window loaded event handler instead
            InitializeComponent();
        }

        private void M_kinectHelper_ImagesArrived(object sender, KinectFrameArgs e)
        {
            Dispatcher.Invoke(new Action(() => UpdatePollTime()));
            if (e.TypeArrived.Equals(FrameType.Alignment))
            {
                Dispatcher.Invoke(new Action(() => m_depthImage = e.DepthImage));
                Dispatcher.Invoke(new Action(() => Al_Image.Source = m_depthImage));
                Dispatcher.Invoke(new Action(() => AutoCalibrator(e.pointCloudV3)));
            }
            else if (e.TypeArrived.Equals(FrameType.Calibration))
            {
                Dispatcher.Invoke(new Action(() => m_depthImage = e.DepthImage));
                Dispatcher.Invoke(new Action(() => Cal_Image.Source = m_depthImage));
            }
            else if (e.TypeArrived.Equals(FrameType.Full))
            {
                Dispatcher.Invoke(new Action(() => m_colorImage = e.ColorImage));
                Dispatcher.Invoke(new Action(() => m_depthImage = e.DepthImage));
                Dispatcher.Invoke(new Action(() => m_infraredImage = e.InfraredImage));
                Dispatcher.Invoke(new Action(() => Scan_ColorImage.Source = m_colorImage));
                Dispatcher.Invoke(new Action(() => Scan_DepthImage.Source = m_depthImage));
                Dispatcher.Invoke(new Action(() => Scan_InfraredImage.Source = m_infraredImage));
            }
        }
        private void UpdatePollTime()
        {
            if (m_lastPoll != 0)
                UI_statusBar.Content = $"Time per poll: {m_stopwatch.ElapsedMilliseconds - m_lastPoll}ms";
            m_lastPoll = m_stopwatch.ElapsedMilliseconds;
        }

        private void M_timer_Tick(object sender, EventArgs e)
        {
            switch (m_currentView)
            {
                case ViewTabs.Align:
                    m_rotationAxis = new Vector3((float)Al_xSlider.Value, 1, (float)Al_zSlider.Value);
                    m_kinectHelper.GetAlignmentFrame(m_boundingContext, m_rotationAxis, (int)Al_mmSlider.Value);
                    break;
                case ViewTabs.Calibrate:
                    float xmin = (float)Cal_xminSlider.Value;
                    float xmax = (float)Cal_xmaxSlider.Value;
                    float ymin = (float)Cal_yminSlider.Value;
                    float ymax = (float)Cal_ymaxSlider.Value;
                    float zmin = (float)Cal_zminSlider.Value;
                    float zmax = (float)Cal_zmaxSlider.Value;
                    m_boundingContext = new BoundingBox(new Vector3(xmin, ymin, zmin), new Vector3(xmax, ymax, zmax));
                    m_kinectHelper.GetCalibrationFrame(m_boundingContext, (int)Cal_sampleSlider.Value, (float)Cal_varianceSlider.Value, (int)Cal_nullPixSlider.Value);
                    break;
            }
            if (!(m_lastPointCloud is null))
            {

            }
        }

        private void UI_TabControl_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            m_currentView = (ViewTabs)UI_TabControl.SelectedIndex;
            switch (m_currentView)
            {
                case ViewTabs.Align:
                    m_timer.Start();
                    break;
                case ViewTabs.Calibrate:
                    m_timer.Start();
                    break;
                case ViewTabs.Scan:
                    m_timer.Stop();
                    break;
                default:
                    m_timer.Stop();
                    break;
            }
        }

        private void Scan_Button_Click(object sender, RoutedEventArgs e)
        {
            if (m_kinectHelper.Connected)
            {
                UI_AlignTab.IsEnabled = false;
                UI_CalibrateTab.IsEnabled = false;
                m_scanner = new Scanner(m_kinectHelper, m_boundingContext, m_rotationAxis, (int)Scan_slider.Value, 75);
                m_scanner.StatusUpdated += Scanner_StatusUpdated;
                Scan_Button.IsEnabled = false;
                m_lastBoundingContext = m_boundingContext;
                m_lastRotationAxis = m_rotationAxis;
                m_lastScanNum = (int)Scan_slider.Value;
            }
        }

        private void Scanner_StatusUpdated(object sender, ScannerEventArgs e)
        {
            if (e.Status == State.Scanning)
                Dispatcher.Invoke(new Action(() => Scan_Progress.Value = e.Completion));
            else if (e.Status == State.Complete)
            {
                Dispatcher.Invoke(new Action(() => m_lastPointCloud = e.PointCloud));
                Dispatcher.Invoke(new Action(() => ScanComplete()));
            }
            else if (e.Status == State.Error)
            {
                //alert user
            }
        }

        private void ScanComplete()
        {
            //UI update
            Scan_Progress.Value = 0;
            Scan_Button.IsEnabled = true;
            UI_AlignTab.IsEnabled = true;
            UI_CalibrateTab.IsEnabled = true;
            UI_ProcessingTab.IsEnabled = true;
            UI_TabControl.SelectedIndex = (int)ViewTabs.Processing;
            //Add Project
            Project proj = new Project(m_lastPointCloud, DateTime.Now, m_lastRotationAxis, m_lastScanNum, m_lastBoundingContext);
            m_projects.Add(proj);
            Pro_projects.Items.Add(proj);
            //Autogenerate mesh
            Generate();
            Pro_generateButton.IsEnabled = true;

        }

        private void M_fileSystemWatcher_Created(object sender, FileSystemEventArgs e)
        {
            Thread.Sleep(2000);
            Dispatcher.Invoke(new Action(() => FileProcessed(e.FullPath)));
        }

        private void FileProcessed(string path)
        {
            UI_statusBar.Content = $"Mesh processed in {m_stopwatch.ElapsedMilliseconds}ms";
            m_lastMesh = FileReader.ReadMFile(path);
            FileWriter.SaveASCIISTL(m_lastMesh, Directory.GetCurrentDirectory() + @"\processing\object.stl", "object");
            Process autoOpen = new Process();
            autoOpen.StartInfo.FileName = Directory.GetCurrentDirectory() + @"\processing\object.stl";
            autoOpen.Start();
            File.Delete(path);
            //Add statistics
            Pro_stats.Items.Clear();
            Pro_stats.Items.Add(new ListViewItem() { Content = $"Total Points: {m_lastStats.TotalPoints}" });
            Pro_stats.Items.Add(new ListViewItem() { Content = $"Reduced Points: {m_lastStats.ReducedPoints}" });
            Pro_stats.Items.Add(new ListViewItem() { Content = $"Octal Nodes: {m_lastStats.OctalNodes}" });
            Pro_stats.Items.Add(new ListViewItem() { Content = $"Octal Dimensions (x,y,z): ({m_lastStats.OctalDim.X.ToString("F")},{m_lastStats.OctalDim.Y.ToString("F")},{m_lastStats.OctalDim.Z.ToString("F")}" });
            Pro_stats.Items.Add(new ListViewItem() { Content = $"Octal Average: {m_lastStats.Average.ToString("F")}" });
            Pro_stats.Items.Add(new ListViewItem() { Content = $"Octal Standard Deviation: {m_lastStats.StandardDeviation.ToString("F")}" });
            //Save project
            Pro_save.IsEnabled = true;
        }

        private void Generate()
        {
            m_stopwatch.Restart();
            Vector3[] conglom = DataProcessing.conglomerateData(m_lastPointCloud, m_lastRotationAxis, 360f / m_lastScanNum);
            m_lastStats.TotalPoints = conglom.Length;
            //Dump points into octtree nodes
            List<OctTree> OctList = new List<OctTree>();
            OctTree octTree = new OctTree(new BoundingBox(m_lastBoundingContext.Min, new Vector3(1, 1, 1)), conglom.Distinct(), OctList, (float)Pro_octreeResolution.Value);
            m_lastStats = new Stats();
            m_lastStats.OctalNodes = OctList.Count;
            m_lastStats.OctalDim = OctList[0].Dims;
            //Do statistical analysis of points
            double octAverage = (float)OctList.Average(x => x.m_Points.Count);
            double octSD = OctList.Select(x => (double)x.m_Points.Count).StandardDeviation();
            double octSOM = octSD / Math.Sqrt(OctList.Count);
            m_lastStats.Average = octAverage;
            m_lastStats.StandardDeviation = octSD;
            //Remove all outliers
            OctList.RemoveAll(x => x.m_Points.Count < octAverage - octSD / 4);
            OctList.ForEach(x => x.m_Points = new List<Vector3>() { new Vector3(x.m_Points.Average(q => q.X), x.m_Points.Average(q => q.Y), x.m_Points.Average(q => q.Z)) });
            //Separate octree into its layers
            var layers = (from q in OctList group q by q.m_Region.Min.Y into r select new { r.Key, List = r.Select(x => x) } into s orderby s.Key select s).ToList();
            List<IEnumerable<Vector3>> goodPoints = new List<IEnumerable<Vector3>>();
            if (Pro_fillBottomCheck.IsChecked == true)
            {
                float averageY = layers.First().List.SelectMany(x => x.m_Points).Average(x => x.Y);
                //Make a bottom layer to be culled
                List<Vector3> bottomMax = new List<Vector3>();
                for (float x = m_lastBoundingContext.Min.X; x < m_lastBoundingContext.Max.X; x += (float)Pro_octreeResolution.Value)
                {
                    for (float z = m_lastBoundingContext.Min.Z; z < m_lastBoundingContext.Max.Z; z += (float)Pro_octreeResolution.Value)
                    {
                        bottomMax.Add(new Vector3(x, averageY, z));
                    }
                }
                //Get bottom layer angle, distance and point for grid
                var botLayerDetails = (from q in bottomMax select new { Angle = DataProcessing.GetAngle(q.X, q.Z, m_lastRotationAxis.X, m_lastRotationAxis.Z), Distance = Math.Sqrt(Math.Pow(q.X - m_lastRotationAxis.X, 2) + Math.Pow(q.Z - m_lastRotationAxis.Z, 2)), Point = q, } into r orderby r.Angle select r).ToList();
                //Get bottom layer angle for
                var bottomTrue = layers.First().List.SelectMany(x => x.m_Points);
                var bottomTrueDetails = (from q in bottomTrue select new { Angle = DataProcessing.GetAngle(q.X, q.Z, m_lastRotationAxis.X, m_lastRotationAxis.Z), Distance = Math.Sqrt(Math.Pow(q.X - m_lastRotationAxis.X, 2) + Math.Pow(q.Z - m_lastRotationAxis.Z, 2)), Point = q, } into r orderby r.Angle select r).ToList();
                double lastAngle = 0;
                List<Vector3> goodBottom = new List<Vector3>();
                for (int i = 0; i < bottomTrueDetails.Count(); i++)
                {
                    var checker = bottomTrueDetails[i];
                    var goodSection = from q in botLayerDetails where q.Angle > lastAngle && q.Angle <= checker.Angle && q.Distance < checker.Distance select q;
                    goodBottom.AddRange(goodSection.Select(x => x.Point));
                    lastAngle = checker.Angle;
                }

                goodPoints.Add(goodBottom);
            }
            if (Pro_fillTopCheck.IsChecked == true)
            {
                float averageY = layers.Last().List.SelectMany(x => x.m_Points).Average(x => x.Y);
                //Make a bottom layer to be culled
                List<Vector3> topMax = new List<Vector3>();
                for (float x = m_lastBoundingContext.Min.X; x < m_lastBoundingContext.Max.X; x += (float)Pro_octreeResolution.Value)
                {
                    for (float z = m_lastBoundingContext.Min.Z; z < m_lastBoundingContext.Max.Z; z += (float)Pro_octreeResolution.Value)
                    {
                        topMax.Add(new Vector3(x, averageY, z));
                    }
                }
                //Get bottom layer angle, distance and point for grid
                var topLayerDetails = (from q in topMax select new { Angle = DataProcessing.GetAngle(q.X, q.Z, m_lastRotationAxis.X, m_lastRotationAxis.Z), Distance = Math.Sqrt(Math.Pow(q.X - m_lastRotationAxis.X, 2) + Math.Pow(q.Z - m_lastRotationAxis.Z, 2)), Point = q, } into r orderby r.Angle select r).ToList();
                //Get bottom layer angle for
                var topTrue = layers.Last().List.SelectMany(x => x.m_Points);
                var topTrueDetails = (from q in topTrue select new { Angle = DataProcessing.GetAngle(q.X, q.Z, m_lastRotationAxis.X, m_lastRotationAxis.Z), Distance = Math.Sqrt(Math.Pow(q.X - m_lastRotationAxis.X, 2) + Math.Pow(q.Z - m_lastRotationAxis.Z, 2)), Point = q, } into r orderby r.Angle select r).ToList();
                double lastAngle = 0;
                List<Vector3> goodTop = new List<Vector3>();
                for (int i = 0; i < topTrueDetails.Count(); i++)
                {
                    var checker = topTrueDetails[i];
                    var goodSection = from q in topLayerDetails where q.Angle > lastAngle && q.Angle <= checker.Angle && q.Distance < checker.Distance select q;
                    goodTop.AddRange(goodSection.Select(x => x.Point));
                    lastAngle = checker.Angle;
                }

                goodPoints.Add(goodTop);
            }

            goodPoints.AddRange(OctList.Select(x => x.m_Points.ToList()));
            Vector3[] final = goodPoints.SelectMany(x => x).ToArray();
            m_lastStats.ReducedPoints = final.Length;
            FileWriter writer = new FileWriter(final, "processing");
            writer.WriteToFile();
            writer.WriteToASC();

            BatchFileManager bfm = new BatchFileManager(Directory.GetCurrentDirectory());
            bfm.createBatchFile("pointcloud", Pro_samplingResolution.Value);
        }

        private void AutoCalibrator(Vector3[] points)
        {
            if (m_autoCal)
            {
                float closestZ = points.Min(q => q.Z);
                Vector3[] frontmost = (from q in points where q.Z >= closestZ - 0.0005 orderby q.Y select q).ToArray();
                Vector3 top = frontmost.Last();
                Vector3 bottom = frontmost.First();
                float x = points.Average(q => q.X);
                float z = frontmost.Average(q => q.Z);
                m_rotationAxis = new Vector3(x, 1, z);
                Al_Stats.Text = $"\tTop:{top.X.ToString("F3")},{top.Y.ToString("F3")},{top.Z.ToString("F3")}\tBottom:{bottom.X.ToString("F3")},{bottom.Y.ToString("F3")},{bottom.Z.ToString("F3")}\tAlignment:{x.ToString("F3")},{1.ToString("F3")},{z.ToString("F3")}";
                Al_xSlider.Value = x;
                Al_zSlider.Value = z;
            }
            else
            {
                Al_Stats.Text = $"\tAlignment:{m_rotationAxis.X.ToString("F3")},{m_rotationAxis.Y.ToString("F3")},{m_rotationAxis.Z.ToString("F3")}";
            }
        }

        private void Al_AutoCal_Checked(object sender, RoutedEventArgs e)
        {
            if (Al_AutoCal.IsChecked.Value)
            {
                Al_xDock.IsEnabled = false;
                Al_zDock.IsEnabled = false;
                m_autoCal = true;
            }
            else
            {
                Al_xDock.IsEnabled = true;
                Al_zDock.IsEnabled = true;
                m_autoCal = false;
            }
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            m_kinectHelper.Close();
            if (!Directory.Exists(Directory.GetCurrentDirectory() + @"\projects\"))
                Directory.CreateDirectory(Directory.GetCurrentDirectory() + @"\projects\");
            string[] files = Directory.GetFiles(Directory.GetCurrentDirectory() + @"\projects\");
            foreach (string item in files)
            {
                File.Delete(item);
            }
            FileStream fs = new FileStream("settings.bin", FileMode.Create);
            m_binaryFormatter.Serialize(fs, new StateSaver(m_rotationAxis, m_lastBoundingContext.Min, m_lastBoundingContext.Max, Cal_varianceSlider.Value, Cal_sampleSlider.Value, Cal_nullPixSlider.Value));
            for (int i = 0; i < m_projects.Count; i++)
            {
                fs = new FileStream(Directory.GetCurrentDirectory() + @"\projects\" + m_projects[i].Date.Year + m_projects[i].Date.Month + m_projects[i].Date.Hour + m_projects[i].Date.Minute + m_projects[i].Date.Second, FileMode.Create);
                m_binaryFormatter.Serialize(fs, m_projects[i]);
            }

        }

        private void Test_Click(object sender, RoutedEventArgs e)
        {
            //Code for testing things here
        }

        private void Pro_generateButton_Click(object sender, RoutedEventArgs e)
        {
            Generate();
        }

        private void Pro_save_Click(object sender, RoutedEventArgs e)
        {
            SaveFileDialog saveFileDialog = new SaveFileDialog();
            //saveFileDialog.Filter = "STL File(.stl)|";
            saveFileDialog.AddExtension = true;
            saveFileDialog.DefaultExt = "stl";
            if (saveFileDialog.ShowDialog() == true)
            {
                string path = saveFileDialog.FileName;
                FileWriter.SaveASCIISTL(m_lastMesh, path, "scan");
            }
        }

        private void Pro_projects_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            if (Pro_projects.SelectedIndex >= 0)
            {
                ProjectLoader(Pro_projects.SelectedIndex);
                Pro_projects.SelectedIndex = -1;
            }
        }

        private void Pro_loadButtonClick(object sender, RoutedEventArgs e)
        {
            if (Pro_projects.SelectedIndex >= 0)
            {
                ProjectLoader(Pro_projects.SelectedIndex);
                Pro_projects.SelectedIndex = -1;
            }
        }

        private void ProjectLoader(int index)
        {
            Project getter = m_projects[index];
            m_lastPointCloud = getter.PointCloud;
            m_lastRotationAxis = getter.RotationAxis;
            m_lastScanNum = getter.ScanNum;
            m_lastBoundingContext = getter.Bounds;
            Pro_generateButton.IsEnabled = true;
            Pro_save.IsEnabled = false;
        }

        private void Pro_projects_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (Pro_projects.SelectedIndex >= 0)
            {
                Pro_loadSelected.IsEnabled = true;
                Pro_deleteSelected.IsEnabled = true;
            }

        }

        private void Pro_deleteSelected_Click(object sender, RoutedEventArgs e)
        {
            if (Pro_projects.SelectedIndex >= 0)
            {
                if (MessageBox.Show("Are you sure you want to delete this project?", "3D Scanner", MessageBoxButton.OKCancel) == MessageBoxResult.OK)
                {
                    Pro_projects.Items.RemoveAt(Pro_projects.SelectedIndex);
                }
            }
        }
    }
    [Serializable]
    class StateSaver
    {
        public Vector3 Al;
        public Vector3 CalMin;
        public Vector3 CalMax;
        public double CalVar;
        public double CalSam;
        public double CalNull;

        public StateSaver(Vector3 ali, Vector3 calmi, Vector3 calma, double calva, double calsam, double calnull)
        {
            Al = ali;
            CalMin = calmi;
            CalMax = calma;
            CalVar = calva;
            CalSam = calsam;
            CalNull = calnull;
        }
    }

    [Serializable]
    class Project
    {
        public DateTime Date { get; set; }
        public Vector3[][] PointCloud { get; set; }
        public Vector3 RotationAxis { get; set; }
        public int ScanNum { get; set; }
        public BoundingBox Bounds { get; set; }
        public int Points
        {
            get
            {
                return PointCloud.Sum(x => x.Count());
            }
        }

        public Project(Vector3[][] pc, DateTime dt, Vector3 rot, int scans, BoundingBox box)
        {
            PointCloud = pc;
            Date = dt;
            RotationAxis = rot;
            ScanNum = scans;
            Bounds = box;
        }
        public override bool Equals(object obj)
        {
            if (!(obj is Project))
                return false;
            return (obj as Project).Date.Equals(Date);
        }
        public override int GetHashCode()
        {
            return Date.GetHashCode();
        }
    }
}
