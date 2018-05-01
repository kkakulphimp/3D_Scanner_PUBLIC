using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Diagnostics;
using System.Threading;

namespace KinectPrototype
{
    //class that builds the batch file which creates the mesh / 3d file 
    public class BatchFileManager
    {
        //path of the directory 
        private string dirpath;

        //batch file name
        private string filename;

        //batch file path;
        string batchFilePath;

        //container for the various commands to be executed in the .bat file
        private List<string> commands = new List<string>();

        //construction
        public BatchFileManager(string dir)
        {
            //set the directory path to the processing folder
            dirpath = dir + @"\processing";
        }

        //create a batch file with the specified configuration options embedded within it,
        public void createBatchFile(string name, double samplingres)
        {
            filename = name;

            //create the batch file
            buildFile(samplingres);

            //execute the batch file
            runFile();
        }

        //create the batch files based on user specifications from app
        private void buildFile(double sampling)
        {
            //build file path from directory path 
            batchFilePath = dirpath + @"\" + filename + ".bat";

            //go through the config controls and build the execution string
            //if (Cont1.ID == true)
            //{
            //}
            //read in the .pts file from cin, set samplingID, and save the result through cout in a reconstructed mesh file
            string ptsfilepath = Directory.GetCurrentDirectory() + @"\processing\" + filename + ".pts";
            string meshfilepath = Directory.GetCurrentDirectory() + @"\processing\" + filename + ".recon.m";
            //string optmeshfilepath = Directory.GetCurrentDirectory() + @"\processing\" + filename + ".opt.m";
            //string subfilepath = Directory.GetCurrentDirectory() + @"\processing\" + filename + ".sub.m ";
            //string subfilepath2 = Directory.GetCurrentDirectory() + @"\processing\" + filename + ".sub2.m ";

            string reconString = "Recon <" + ptsfilepath + " -samplingd " + sampling.ToString("F5") + " >" + meshfilepath ;
            //string meshfitString = "Meshfit -mfile " + meshfilepath + " -file " + ptsfilepath + " -crep 1e-5 -reconstruct > " + optmeshfilepath;
            //todo patch these guys in for better resolution stl
            //string filtermeshString = "Filtermesh " + optmeshfilepath + "-angle 55 -mark | Subdivfit -mfile - -file " + ptsfilepath + " - crep 1e-5 - csharp .2e-5 - reconstruct >" + subfilepath;
            //string subdivString = "";
          

            commands.Add("cd ..");
            commands.Add("cd ..");
            commands.Add("cd ..");
            commands.Add("cd ..");
            commands.Add("cd Mesh-processing-library");
            commands.Add("cd executables");
            commands.Add(reconString);

            //convert the mesh that we got to an stl file
            //leveraging c++ code
            //string mesh2stlString = "mesh2stl <" + filename + ".recon.m >" + filename + ".stl";
            //commands.Add(mesh2stlString);

            //currently using karuns c# filewriter

            

            //}

            if (!Directory.Exists(dirpath))
            {
                Directory.CreateDirectory(dirpath);
            }

            using (StreamWriter outputFile = new StreamWriter(batchFilePath))
            {
                //build batch file here
                outputFile.WriteLine("@echo off");
                //outputFile.WriteLine("setlocal cd \"%~p0\""); //change the dir 
                commands.ForEach(q => outputFile.WriteLine(q)); //dump the commands into the file
                outputFile.Close();
            }
        }

        //execute the file
        private void runFile()
        {
            Process process = new Process();
            try
            {
                process.StartInfo.UseShellExecute = false;
                process.StartInfo.FileName = batchFilePath;
                process.StartInfo.CreateNoWindow = true;
                process.StartInfo.WorkingDirectory = Directory.GetCurrentDirectory() + @"\processing\";
                process.Start();
                process.WaitForExit();
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
            }
        }
    }
}
