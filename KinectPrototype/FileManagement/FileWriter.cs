using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Xna.Framework;
using System.IO;
using System.Windows.Media.Media3D;
using System.Windows;

namespace FileManagement
{
    //writes the point cloud to a .pts file in order to be used by c++ meshing code
    public class FileWriter
    {
        private Vector3[] pointCloud;
        string path;
        string location;

        public FileWriter(Vector3[] pc, string fileloc)
        {
            pointCloud = pc;

            //create the directory if needed
            if (!Directory.Exists(fileloc))
            {
                Directory.CreateDirectory(fileloc);
            }
            location = fileloc;
        }

        public void WriteToFile()
        {
            path = location + @"\pointcloud.pts";
            if (File.Exists(path))
                File.Delete(path);
            using (StreamWriter outputFile = new StreamWriter(path))
            {
                //inspect each point
                for (int i = 0; i < pointCloud.Length - 1; i++)
                {
                    string line = "";
                    Vector3 point = pointCloud[i];

                    //format the data
                    line = "p " + point.X + " " + point.Y + " " + point.Z;

                    //write to the file
                    outputFile.WriteLine(line);
                }

                outputFile.Close();
            }
        }
        public void WriteToASC()
        {
            path = location + @"\pointcloud.asc";
            if (File.Exists(path))
                File.Delete(path);
            using (StreamWriter outputFile = new StreamWriter(path))
            {
                //inspect each point
                for (int i = 0; i < pointCloud.Length - 1; i++)
                {
                    string line = "";
                    Vector3 point = pointCloud[i];

                    //format the data
                    line = point.X + " " + point.Y + " " + point.Z;

                    //write to the file
                    outputFile.WriteLine(line);
                }

                outputFile.Close();
            }
        }
        public static void SaveASCIISTL(MeshGeometry3D mesh, string path, string name)
        {
            try
            {
                using(FileStream fs = new FileStream(path, FileMode.Create))
                using (StreamWriter outputFile = new StreamWriter(fs))
                {
                    outputFile.WriteLine("solid " + name);
                    Point3D[] vertices = new Point3D[mesh.Positions.Count];
                    Vector3D[] normals = new Vector3D[mesh.Normals.Count];
                    int[] indices = new int[mesh.TriangleIndices.Count];

                    mesh.Positions.CopyTo(vertices, 0);
                    mesh.Normals.CopyTo(normals, 0);
                    mesh.TriangleIndices.CopyTo(indices, 0);

                    for (int i = 0; i < indices.Length; i += 3)
                    {
                        outputFile.WriteLine($"  facet normal {normals[i / 3].X.ToString("F17")} {normals[i / 3].Y.ToString("F17")} {normals[i / 3].Z.ToString("F17")}");
                        outputFile.WriteLine($"    outer loop");
                        outputFile.WriteLine($"      vertex {vertices[indices[i]].X.ToString("F17")} {vertices[indices[i]].Y.ToString("F17")} {vertices[indices[i]].Z.ToString("F17")}");
                        outputFile.WriteLine($"      vertex {vertices[indices[i + 1]].X.ToString("F17")} {vertices[indices[i + 1]].Y.ToString("F17")} {vertices[indices[i + 1]].Z.ToString("F17")}");
                        outputFile.WriteLine($"      vertex {vertices[indices[i + 2]].X.ToString("F17")} {vertices[indices[i + 2]].Y.ToString("F17")} {vertices[indices[i + 2]].Z.ToString("F17")}");
                        outputFile.WriteLine($"    endloop");
                        outputFile.WriteLine($"  endfacet");
                    }
                    outputFile.WriteLine("endsolid");
                }
            }
            catch(Exception error)
            {
                Console.WriteLine(error);
            }

        }
    }
}
