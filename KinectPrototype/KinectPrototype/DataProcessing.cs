using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Xna.Framework;

namespace KinectPrototype
{
    public class DataProcessing
    {
        public static Vector3[] conglomerateData (IEnumerable<Vector3[]> scans, Vector3 center, float stepAngleInDegrees)
        {
            Vector3[] returnArray = new Vector3[scans.ToList().Sum(x => x.Count())];
            Vector3[][] temp = scans.ToArray();
            float currentRot = 0;
            int index = 0;
            int scanNum = scans.Count();

            for (int i = 0; i < temp.Count(); i++)
            {
                for (int j = 0; j < temp[i].Count(); j++)
                {
                    returnArray[index++] = rotate(temp[i][j], center, currentRot);
                }
                currentRot += 360 / scanNum;
            }

            return returnArray;
        }
        public static double GetAngle(double x, double z, double ox, double oz)
        {
            return Math.Atan2(oz, ox) - Math.Atan2(z, x);
        }

        private static Vector3 rotate(Vector3 point, Vector3 pivot, float angleInDegrees)
        {
            float angleInRad = (float)(angleInDegrees * (Math.PI / 180));
            float cosTheta = (float)Math.Cos(angleInRad);
            float sinTheta = (float)Math.Sin(angleInRad);

            return new Vector3
            {
                X =
                cosTheta * (point.X - pivot.X) -
                sinTheta * (point.Z - pivot.Z) + pivot.X,
                Y = point.Y,
                Z =
                sinTheta * (point.X - pivot.X) +
                cosTheta * (point.Z - pivot.Z) + pivot.Z
            };
        }
    }
}
