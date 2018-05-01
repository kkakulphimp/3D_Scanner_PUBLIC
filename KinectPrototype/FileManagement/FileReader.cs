using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;
using Microsoft.Xna.Framework;
using System.Windows.Media.Media3D;
using System.Windows.Media;
using Microsoft.Win32;


namespace FileManagement
{
    public class FileReader
    {

        public FileReader()
        {
            //MeshGeometry3D m3d = ReadMFile(location);
            //SaveFileDialog sfd = new SaveFileDialog();
            //sfd.DefaultExt = "stl";
            //sfd.FileName = "object.stl";
            //if (sfd.ShowDialog() == true)
            //{
            //    FileWriter.SaveASCIISTL(m3d, sfd.FileName, "object");
            //}
        }

        public static MeshGeometry3D ReadMFile(string location)
        {
            string[] fileLines;
            List<Vector3> vertices = new List<Vector3>();
            List<int> indeces = new List<int>();
            List<Vector3> normals = new List<Vector3>();
            MeshGeometry3D thing = new MeshGeometry3D();
            fileLines = File.ReadAllLines(location, Encoding.ASCII);
            for (int i = 0; i < fileLines.Length; i++)
            {
                string[] line = fileLines[i].Split(null).Where(x => !string.IsNullOrWhiteSpace(x)).ToArray();
                //Get all vertices
                if (line[0].Equals("Vertex"))
                {
                    Vector3 vertex = new Vector3(float.Parse(line[2]), float.Parse(line[3]), float.Parse(line[4]));
                    vertices.Add(vertex);

                    }
                //Get all triangle indeces
                else if (line[0].Equals("Face"))
                {
                    //Indeces in m file are 1 indexed for some reason
                    indeces.Add(int.Parse(line[2]) - 1);
                    indeces.Add(int.Parse(line[3]) - 1);
                    indeces.Add(int.Parse(line[4]) - 1);
                }
            }
            //Calculate normals
            for (int i = 0; i < indeces.Count(); i += 3)
            {
                Vector3 v1 = vertices[indeces[i]];
                Vector3 v2 = vertices[indeces[i + 1]];
                Vector3 v3 = vertices[indeces[i + 2]];
                Vector3 temp = CalculateSurfaceNormal(v1, v2, v3);
                normals.Add(temp);
            }
            Int32Collection ind = new Int32Collection(indeces);
            Point3DCollection vert = new Point3DCollection(vertices.Select(x => new Point3D(x.X, x.Y, x.Z)));
            Vector3DCollection norm = new Vector3DCollection(normals.Select(x => new Vector3D(x.X, x.Y, x.Z)));

            thing.TriangleIndices = ind;
            thing.Positions = vert;
            thing.Normals = norm;
            return thing;
        }
        public static Vector3 CalculateSurfaceNormal(Vector3 v1, Vector3 v2, Vector3 v3)
        {
            Vector3 normal = new Vector3();

            Vector3 U = v2 - v1;
            Vector3 V = v3 - v1;

            normal.X = (U.Y * V.Z) - (U.Z * V.Y);
            normal.Y = (U.Z * V.X) - (U.X * V.Z);
            normal.Z = (U.X * V.Y) - (U.Y * V.X);

            return normal;
        }
    }
}
