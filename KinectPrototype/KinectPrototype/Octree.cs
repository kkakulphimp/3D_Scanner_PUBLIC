using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Media.Media3D;
using Microsoft.Xna.Framework;

namespace KinectPrototype
{
    class OctTree
    {
        //Node's region
        public BoundingBox m_Region { get; set; }
        public List<Vector3> m_Points { get; set; }
        public Vector3 Dims { get; private set; }
        private Queue<Vector3> m_PendingInsertion;

        private OctTree[] m_ChildNode = new OctTree[8];
        //Smallest region
        private float m_baseSize;

        /// <summary>
        /// Insertion point
        /// </summary>
        /// <param name="region"></param>
        /// <param name="master"></param>
        public OctTree(
            BoundingBox masterRegion,
            IEnumerable<Vector3> masterColl,
            List<OctTree> baseList,
            float basesize = 0.010f)
        {
            List<Vector3> masterList = masterColl.ToList();
            if (masterList.Count.Equals(0))
                return;
            m_Points = masterList;
            m_baseSize = basesize;
            //Set region's points
            m_Region = masterRegion;
            //Get dimension of master box
            Vector3 dimensions = m_Region.Max - m_Region.Min;
            //Halfway point of box
            Vector3 half = dimensions / 2f;
            //Center of box
            Vector3 center = m_Region.Min + half;
            //Smallest size box, send back to baselist
            if (dimensions.X <= m_baseSize && dimensions.Y <= m_baseSize
                && dimensions.Z <= m_baseSize)
            {
                baseList.Add(this);
                Dims = dimensions;
            }
            else
            {
                bool firstpass = true;
                //Create set of boxes
                BoundingBox[] boxes = new BoundingBox[8];
                //Create holder for points
                List<Vector3>[] boxPoints = new List<Vector3>[8];
                m_PendingInsertion = new Queue<Vector3>(masterList);
                //Spin up all individual boxes
                boxes[0] = new BoundingBox(m_Region.Min, center);
                boxes[1] = new BoundingBox(new Vector3(center.X, m_Region.Min.Y, m_Region.Min.Z), new Vector3(m_Region.Max.X, center.Y, center.Z));
                boxes[2] = new BoundingBox(new Vector3(center.X, m_Region.Min.Y, center.Z),new Vector3(m_Region.Max.X, center.Y, m_Region.Max.Z));
                boxes[3] = new BoundingBox(new Vector3(m_Region.Min.X, m_Region.Min.Y, center.Z),new Vector3(center.X, center.Y, m_Region.Max.Z));
                boxes[4] = new BoundingBox(new Vector3(m_Region.Min.X, center.Y, m_Region.Min.Z),new Vector3(center.X, m_Region.Max.Y, center.Z));
                boxes[5] = new BoundingBox(new Vector3(center.X, center.Y, m_Region.Min.Z),new Vector3(m_Region.Max.X, m_Region.Max.Y, center.Z));
                boxes[6] = new BoundingBox(center, m_Region.Max);
                boxes[7] = new BoundingBox(new Vector3(m_Region.Min.X, center.Y, center.Z),new Vector3(center.X, m_Region.Max.Y, m_Region.Max.Z));

                while (m_PendingInsertion.Count > 0)
                {
                    Vector3 temp = m_PendingInsertion.Dequeue();
                    for (int i = 0; i < boxes.Length; i++)
                    {
                        //instantite list on first pass
                        if (firstpass)
                            boxPoints[i] = new List<Vector3>();
                        //find container box until binnable
                        if (
                            boxes[i].Contains(temp).Equals(ContainmentType.Contains)
                            || boxes[i].Contains(temp).Equals(ContainmentType.Intersects))
                        {
                            boxPoints[i].Add(temp);
                        }
                    }
                    if (firstpass)
                        firstpass = false;
                }
                for (int i = 0; i < boxPoints.Length; i++)
                {
                    if (boxPoints[i].Count > 0)
                    {
                        //Assign child
                        m_ChildNode[i] = new OctTree(boxes[i], boxPoints[i], baseList, basesize);
                    }
                }
            }
        }
    }
}
