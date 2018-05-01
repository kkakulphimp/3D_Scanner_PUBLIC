using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Xna.Framework;


namespace KinectPrototype
{
    class Calibrator
    {
        Vector3[][] scans;
        List<Vector3> closestPoints;
        float os;

        public Calibrator(Vector3[][] input, float offset)
        {
            scans = input;
            closestPoints = new List<Vector3>();
            os = offset;
        }

        public Vector3 getCenter()
        {
            for (int i = 0; i < scans.Length - 1; i++)
            {
                Vector3[] scan = scans[i];

                Vector3 zMinPoint = new Vector3(0, 0, float.MaxValue);

                for (int j = 0; j < scan.Length - 1; j++)
                {
                    if (scan[j].Z < zMinPoint.Z)
                    {
                        zMinPoint = scan[j];
                    }
                }

                closestPoints.Add(zMinPoint);
            }

            //calculate average x and z

            return computeAverage(closestPoints);

        }

        private Vector3 computeAverage(List<Vector3> list)
        {
            float xSum = 0;
            float zSum = 0;

            list.ForEach(q => {
                xSum += q.X;
                zSum += q.Z;
                });

            return new Vector3(xSum / list.Count, 0, zSum / list.Count + os);
        }
    }
}
