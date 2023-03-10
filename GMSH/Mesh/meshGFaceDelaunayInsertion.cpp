// Gmsh - Copyright (C) 1997-2013 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@geuz.org>.

#include <set>
#include <map>
#include <algorithm>
#include "GmshMessage.h"
#include "OS.h"
#include "robustPredicates.h"
#include "BackgroundMesh.h"
#include "meshGFaceDelaunayInsertion.h"
#include "meshGFaceOptimize.h"
#include "meshGFace.h"
#include "GFace.h"
#include "Numeric.h"
#include "STensor3.h"
#include "Context.h"
#include "MLine.h"
#include "MQuadrangle.h"
#include "Field.h"
#include "GModel.h"
#include "GFaceCompound.h"
#include "intersectCurveSurface.h"
#include "surfaceFiller.h"

double LIMIT_ = 0.5 * sqrt(2.0) * 1;
int  N_GLOBAL_SEARCH;
int  N_SEARCH;
double DT_INSERT_VERTEX;
int MTri3::radiusNorm = 2;

/*
static bool isBoundary(MTri3 *t, double limit_, int &active)
{
  if (t->isDeleted()) return false;
  for (active = 0; active < 3; active++){
    MTri3 *neigh = t->getNeigh(active);
    if (!neigh) return true;
  }
  return false;
}
*/
template <class ITERATOR>
void _printTris(char *name, ITERATOR it,  ITERATOR end, bidimMeshData & data, bool param=true)
{
  FILE *ff = fopen (name,"w");
  fprintf(ff,"View\"test\"{\n");
  while ( it != end ){
    MTri3 *worst = *it;
    if (!worst->isDeleted()){
      if (param)
        fprintf(ff,"ST(%g,%g,%g,%g,%g,%g,%g,%g,%g) {%g,%g,%g};\n",
                data.Us[data.getIndex((worst)->tri()->getVertex(0))],
                data.Vs[data.getIndex((worst)->tri()->getVertex(0))],
                0.0,
                data.Us[data.getIndex((worst)->tri()->getVertex(1))],
                data.Vs[data.getIndex((worst)->tri()->getVertex(1))],
                0.0,
                data.Us[data.getIndex((worst)->tri()->getVertex(2))],
                data.Vs[data.getIndex((worst)->tri()->getVertex(2))],
                0.0,
                (worst)->getRadius(),
                (worst)->getRadius(),
                (worst)->getRadius());
      else
        fprintf(ff,"ST(%g,%g,%g,%g,%g,%g,%g,%g,%g) {%g,%g,%g};\n",
                (worst)->tri()->getVertex(0)->x(),
                (worst)->tri()->getVertex(0)->y(),
                (worst)->tri()->getVertex(0)->z(),
                (worst)->tri()->getVertex(1)->x(),
                (worst)->tri()->getVertex(1)->y(),
                (worst)->tri()->getVertex(1)->z(),
                (worst)->tri()->getVertex(2)->x(),
                (worst)->tri()->getVertex(2)->y(),
                (worst)->tri()->getVertex(2)->z(),
                (worst)->getRadius(),
                (worst)->getRadius(),
                (worst)->getRadius()
                );
    }
    ++it;
  }
  fprintf(ff,"};\n");
  fclose (ff);
}


static bool isActive(MTri3 *t, double limit_, int &active)
{
  if (t->isDeleted()) return false;
  for (active = 0; active < 3; active++){
    MTri3 *neigh = t->getNeigh(active);
    if (!neigh || (neigh->getRadius() < limit_ && neigh->getRadius() > 0)) {
      return true;
    }
  }
  return false;
}

static bool isActive(MTri3 *t, double limit_, int &i, std::set<MEdge,Less_Edge> *front)
{
  if (t->isDeleted()) return false;
  for (i = 0; i < 3; i++){
    MTri3 *neigh = t->getNeigh(i);
    if (!neigh || (neigh->getRadius() < limit_ && neigh->getRadius() > 0)) {
      int ip1 = i - 1 < 0 ? 2 : i - 1;
      int ip2 = i;
      MEdge me (t->tri()->getVertex(ip1),t->tri()->getVertex(ip2));
      if(front->find(me) != front->end()){
	return true;
      }
    }
  }
  return false;
}

static void updateActiveEdges(MTri3 *t, double limit_, std::set<MEdge,Less_Edge> &front)
{
  if (t->isDeleted()) return;
  for (int active = 0; active < 3; active++){
    MTri3 *neigh = t->getNeigh(active);
    if (!neigh || (neigh->getRadius() < limit_ && neigh->getRadius() > 0)) {
      int ip1 = active - 1 < 0 ? 2 : active - 1;
      int ip2 = active;
      MEdge me (t->tri()->getVertex(ip1),t->tri()->getVertex(ip2));
      front.insert(me);
    }
  }
}

bool circumCenterMetricInTriangle(MTriangle *base, const double *metric, bidimMeshData & data )
{
  double R, x[2], uv[2];
  circumCenterMetric(base, metric, data, x, R);
  return invMapUV(base, x, data, uv, 1.e-8);
}

void circumCenterMetric(double *pa, double *pb, double *pc,
                        const double *metric, double *x, double &Radius2)
{
  // d = (u2-u1) M (u2-u1) = u2 M u2 + u1 M u1 - 2 u2 M u1
  double sys[2][2];
  double rhs[2];

  const double a = metric[0];
  const double b = metric[1];
  const double d = metric[2];

  sys[0][0] = 2. * a * (pa[0] - pb[0]) + 2. * b * (pa[1] - pb[1]);
  sys[0][1] = 2. * d * (pa[1] - pb[1]) + 2. * b * (pa[0] - pb[0]);
  sys[1][0] = 2. * a * (pa[0] - pc[0]) + 2. * b * (pa[1] - pc[1]);
  sys[1][1] = 2. * d * (pa[1] - pc[1]) + 2. * b * (pa[0] - pc[0]);

  rhs[0] =
    a * (pa[0] * pa[0] - pb[0] * pb[0]) +
    d * (pa[1] * pa[1] - pb[1] * pb[1]) +
    2. * b * (pa[0] * pa[1] - pb[0] * pb[1]);
  rhs[1] =
    a * (pa[0] * pa[0] - pc[0] * pc[0]) +
    d * (pa[1] * pa[1] - pc[1] * pc[1]) +
    2. * b * (pa[0] * pa[1] - pc[0] * pc[1]);

  if (!sys2x2(sys, rhs, x)){
    //    printf("%g %g %g\n",a,b,d);
  }

  Radius2 =
    (x[0] - pa[0]) * (x[0] - pa[0]) * a +
    (x[1] - pa[1]) * (x[1] - pa[1]) * d +
    2. * (x[0] - pa[0]) * (x[1] - pa[1]) * b;
}

void circumCenterMetricXYZ(double *p1, double *p2, double *p3, SMetric3 & metric,
                           double *res, double *uv, double &radius)
{
  double v1[3] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
  double v2[3] = {p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2]};
  double vx[3] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
  double vy[3] = {p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2]};
  double vz[3]; prodve(vx, vy, vz); prodve(vz, vx, vy);
  norme(vx); norme(vy); norme(vz);
  double p1P[2] = {0.0, 0.0};
  double p2P[2]; prosca(v1, vx, &p2P[0]); prosca(v1, vy, &p2P[1]);
  double p3P[2]; prosca(v2, vx, &p3P[0]); prosca(v2, vy, &p3P[1]);
  double resP[2];

  fullMatrix<double> T(3,3);
  for (int i = 0; i < 3; i++)T(0,i) = vx[i];
  for (int i = 0; i < 3; i++)T(1,i) = vy[i];
  for (int i = 0; i < 3; i++)T(2,i) = vz[i];
  SMetric3 tra = metric.transform(T);
  double mm[3] = {tra(0,0),tra(0,1),tra(1,1)};

  circumCenterMetric(p1P, p2P, p3P, mm, resP, radius);

  if(uv){
    double mat[2][2] = {{p2P[0] - p1P[0], p3P[0] - p1P[0]},
                        {p2P[1] - p1P[1], p3P[1] - p1P[1]}};
    double rhs[2] = {resP[0] - p1P[0], resP[1] - p1P[1]};
    sys2x2(mat, rhs, uv);
  }

  res[0] = p1[0] + resP[0] * vx[0] + resP[1] * vy[0];
  res[1] = p1[1] + resP[0] * vx[1] + resP[1] * vy[1];
  res[2] = p1[2] + resP[0] * vx[2] + resP[1] * vy[2];
}

void circumCenterMetric(MTriangle *base, const double *metric, bidimMeshData & data,
                        double *x, double &Radius2)
{
  // d = (u2-u1) M (u2-u1) = u2 M u2 + u1 M u1 - 2 u2 M u1
  int index0 = data.getIndex (base->getVertex(0)); 
  int index1 = data.getIndex (base->getVertex(1)); 
  int index2 = data.getIndex (base->getVertex(2)); 
  double pa[2] = {data.Us[index0], data.Vs[index0] };
  double pb[2] = {data.Us[index1], data.Vs[index1] };
  double pc[2] = {data.Us[index2], data.Vs[index2] };
  circumCenterMetric(pa, pb, pc, metric, x, Radius2);
}

void buildMetric(GFace *gf, double *uv, double *metric)
{
  Pair<SVector3, SVector3> der = gf->firstDer(SPoint2(uv[0], uv[1]));

  metric[0] = dot(der.first(), der.first());
  metric[1] = dot(der.second(), der.first());
  metric[2] = dot(der.second(), der.second());
}

// m 3x3
// d 3x2
// M = d^T m d

void buildMetric(GFace *gf, double *uv, const SMetric3 & m, double *metric)
{

  Pair<SVector3, SVector3> der = gf->firstDer(SPoint2(uv[0], uv[1]));

  SVector3 x1(m(0,0) * der.first().x() +
              m(1,0) * der.first().y() +
              m(2,0) * der.first().z(),
              m(0,1) * der.first().x() +
              m(1,1) * der.first().y() +
              m(2,1) * der.first().z(),
              m(0,2) * der.first().x() +
              m(1,2) * der.first().y() +
              m(2,2) * der.first().z());
  SVector3 x2(m(0,0) * der.second().x() +
              m(1,0) * der.second().y() +
              m(2,0) * der.second().z(),
              m(0,1) * der.second().x() +
              m(1,1) * der.second().y() +
              m(2,1) * der.second().z(),
              m(0,2) * der.second().x() +
              m(1,2) * der.second().y() +
              m(2,2) * der.second().z());

  metric[0] = dot(x1, der.first());
  metric[1] = dot(x2, der.first());
  metric[2] = dot(x2, der.second());
}

int inCircumCircleAniso(GFace *gf, double *p1, double *p2, double *p3,
                        double *uv, double *metric)
{
  double x[2], Radius2;
  circumCenterMetric(p1, p2, p3, metric, x, Radius2);
  const double a = metric[0];
  const double b = metric[1];
  const double d = metric[2];
  const double d0 = (x[0] - uv[0]);
  const double d1 = (x[1] - uv[1]);
  const double d3 =  d0*d0*a + d1*d1*d + 2.0*d0*d1*b;
  return d3 < Radius2;
}

int inCircumCircleAniso(GFace *gf, MTriangle *base,
                        const double *uv, const double *metricb,
			bidimMeshData & data)
{
  double x[2], Radius2;
  double metric[3];
  if (!metricb){
    int index0 = data.getIndex (base->getVertex(0)); 
    int index1 = data.getIndex (base->getVertex(1)); 
    int index2 = data.getIndex (base->getVertex(2)); 
    double pa[2] = {(data.Us[index0] +data.Us[index1] + data.Us[index2]) / 3.,
  		  (data.Vs[index0] +data.Vs[index1] + data.Vs[index2]) / 3.};
    buildMetric(gf, pa, metric);
  }
  else {metric[0] = metricb[0];metric[1] = metricb[1];metric[2] = metricb[2];};
  circumCenterMetric(base, metric, data, x, Radius2);

  const double a = metric[0];
  const double b = metric[1];
  const double d = metric[2];

  const double d0 = (x[0] - uv[0]);
  const double d1 = (x[1] - uv[1]);
  const double d3 =  d0*d0*a + d1*d1*d + 2.0*d0*d1*b;
  return d3 < Radius2;
}

MTri3::MTri3(MTriangle *t, double lc, SMetric3 *metric, bidimMeshData * data, GFace *gf)
  : deleted(false), base(t)
{
  neigh[0] = neigh[1] = neigh[2] = 0;
  double center[3];
  double pa[3] =
    {base->getVertex(0)->x(), base->getVertex(0)->y(), base->getVertex(0)->z()};
  double pb[3] =
    {base->getVertex(1)->x(), base->getVertex(1)->y(), base->getVertex(1)->z()};
  double pc[3] =
    {base->getVertex(2)->x(), base->getVertex(2)->y(), base->getVertex(2)->z()};

  if (!metric){
    if (radiusNorm == 2){
      circumCenterXYZ(pa, pb, pc, center);
      const double dx = base->getVertex(0)->x() - center[0];
      const double dy = base->getVertex(0)->y() - center[1];
      const double dz = base->getVertex(0)->z() - center[2];
      circum_radius = sqrt(dx * dx + dy * dy + dz * dz);
      //      printf("%p %p %p %g %g %g %g %g %g %g %g %g circ %g lc %g\n",base->getVertex(0),base->getVertex(1),base->getVertex(2),pa[0],pa[1],pa[2],pb[0], pb[1],pb[2],pc[0],pc[1],pc[2],circum_radius,lc);
      circum_radius /= lc;
    }
    else {

      int index0 = data->getIndex (base->getVertex(0)); 
      int index1 = data->getIndex (base->getVertex(1)); 
      int index2 = data->getIndex (base->getVertex(2)); 
      double p1[2] = {data->Us[index0], data->Vs[index0] };
      double p2[2] = {data->Us[index1], data->Vs[index1] };
      double p3[2] = {data->Us[index2], data->Vs[index2] };

      double midpoint[2] = {(p1[0]+p2[0]+p3[0])/3.0,(p1[1]+p2[1]+p3[1])/3.0};

      double quadAngle = backgroundMesh::current() ?
        backgroundMesh::current()->getAngle (midpoint[0],midpoint[1],0) : 0.0;

      double x0 =  p1[0] * cos(quadAngle) + p1[1] * sin(quadAngle);
      double y0 = -p1[0] * sin(quadAngle) + p1[1] * cos(quadAngle);
      double x1 =  p2[0] * cos(quadAngle) + p2[1] * sin(quadAngle);
      double y1 = -p2[0] * sin(quadAngle) + p2[1] * cos(quadAngle);
      double x2 =  p3[0] * cos(quadAngle) + p3[1] * sin(quadAngle);
      double y2 = -p3[0] * sin(quadAngle) + p3[1] * cos(quadAngle);
      double xmax = std::max(std::max(x0,x1),x2);
      double ymax = std::max(std::max(y0,y1),y2);
      double xmin = std::min(std::min(x0,x1),x2);
      double ymin = std::min(std::min(y0,y1),y2);

      double metric[3];
      buildMetric(gf, midpoint, metric);
      double RATIO = 1./pow(metric[0]*metric[2]-metric[1]*metric[1],0.25);

      circum_radius = std::max(xmax-xmin,ymax-ymin) / RATIO;
      circum_radius /= lc;
    }
  }
  else{
    double uv[2];
    circumCenterMetricXYZ(pa, pb, pc, *metric, center, uv, circum_radius);
  }
}

int MTri3::inCircumCircle(const double *p) const
{
  double pa[3] =
    {base->getVertex(0)->x(), base->getVertex(0)->y(), base->getVertex(0)->z()};
  double pb[3] =
    {base->getVertex(1)->x(), base->getVertex(1)->y(), base->getVertex(1)->z()};
  double pc[3] =
    {base->getVertex(2)->x(), base->getVertex(2)->y(), base->getVertex(2)->z()};
  double fourth[3];
  fourthPoint(pa, pb, pc, fourth);

  double result = robustPredicates::insphere(pa, pb, pc, fourth, (double*)p) *
    robustPredicates::orient3d(pa, pb, pc,fourth);
  return (result > 0) ? 1 : 0;
}

int inCircumCircle(MTriangle *base,
                   const double *p, const double *param , bidimMeshData & data)
{
  int index0 = data.getIndex (base->getVertex(0)); 
  int index1 = data.getIndex (base->getVertex(1)); 
  int index2 = data.getIndex (base->getVertex(2)); 
  double pa[2] = {data.Us[index0], data.Vs[index0] };
  double pb[2] = {data.Us[index1], data.Vs[index1] };
  double pc[2] = {data.Us[index2], data.Vs[index2] };

  double result = robustPredicates::incircle(pa, pb, pc, (double*)param) *
    robustPredicates::orient2d(pa, pb, pc);
  return (result > 0) ? 1 : 0;
}

template <class ITER>
void connectTris(ITER beg, ITER end)
{
  std::set<edgeXface> conn;
  while (beg != end){
    if (!(*beg)->isDeleted()){
      for (int i = 0; i < 3; i++){
        edgeXface fxt(*beg, i);
	std::set<edgeXface>::iterator found = conn.find(fxt);
        if (found == conn.end())
	  conn.insert(fxt);
        else if (found->t1 != *beg){
          found->t1->setNeigh(found->i1, *beg);
          (*beg)->setNeigh(i, found->t1);
        }
      }
    }
    ++beg;
  }
}

template <class ITER>
void connectTris_vector(ITER beg, ITER end)
{
  std::vector<edgeXface> conn;
  //  std::set<edgeXface> conn;
  while (beg != end){
    if (!(*beg)->isDeleted()){
      for (int i = 0; i < 3; i++){
        edgeXface fxt(*beg, i);
	//        std::set<edgeXface>::iterator found = conn.find(fxt);
	std::vector<edgeXface>::iterator found  = std::find(conn.begin(), conn.end(), fxt);
        if (found == conn.end())
	  conn.push_back(fxt);
	  //          conn.insert(fxt);
        else if (found->t1 != *beg){
          found->t1->setNeigh(found->i1, *beg);
          (*beg)->setNeigh(i, found->t1);
        }
      }
    }
    ++beg;
  }
}



void connectTriangles(std::list<MTri3*> &l)
{
  connectTris(l.begin(), l.end());
}

void connectTriangles(std::vector<MTri3*> &l)
{
  connectTris(l.begin(), l.end());
}

void connectTriangles(std::set<MTri3*, compareTri3Ptr> &l)
{
  connectTris(l.begin(), l.end());
}

void recurFindCavity(std::list<edgeXface> &shell, std::list<MTri3*> &cavity,
                     double *v, double *param, MTri3 *t,  bidimMeshData & data)
{
  t->setDeleted(true);
  // the cavity that has to be removed because it violates delaunay
  // criterion
  cavity.push_back(t);

  for (int i = 0; i < 3; i++){
    MTri3 *neigh =  t->getNeigh(i) ;
    if (!neigh)
      shell.push_back(edgeXface(t, i));
    else if (!neigh->isDeleted()){
      int circ =  inCircumCircle(neigh->tri(), v , param, data);
      if (circ)
        recurFindCavity(shell, cavity, v, param, neigh, data);
      else
        shell.push_back(edgeXface(t, i));
    }
  }
}

void recurFindCavityAniso(GFace *gf,
                          std::list<edgeXface> &shell, std::list<MTri3*> &cavity,
                          double *metric, double *param,  MTri3 *t, bidimMeshData & data)
{
  t->setDeleted(true);
  // the cavity that has to be removed because it violates delaunay
  // criterion
  cavity.push_back(t);

  for (int i = 0; i < 3; i++){
    MTri3 *neigh = t->getNeigh(i) ;
    edgeXface exf (t, i);
    // take care of untouchable internal edges
    std::set<MEdge,Less_Edge>::iterator it = data.internalEdges.find(MEdge(exf.v[0],exf.v[1])); 
    if (!neigh || it != data.internalEdges.end())
      shell.push_back(exf);
    else  if (!neigh->isDeleted()){
      //      int circ =  inCircumCircleAniso(gf, neigh->tri(), param, metric, data);
      int circ =  inCircumCircleAniso(gf, neigh->tri(), param, metric, data);
      if (circ)
        recurFindCavityAniso(gf, shell, cavity,metric, param, neigh, data);
      else
        shell.push_back(exf);
    }
  }
}

bool circUV(MTriangle *t, bidimMeshData & data, double *res, GFace *gf)
{
  int index0 = data.getIndex (t->getVertex(0)); 
  int index1 = data.getIndex (t->getVertex(1)); 
  int index2 = data.getIndex (t->getVertex(2)); 
  double u1[3] = {data.Us[index0], data.Vs[index0], 0 };
  double u2[3] = {data.Us[index1], data.Vs[index1], 0 };
  double u3[3] = {data.Us[index2], data.Vs[index2], 0 };
  circumCenterXY(u1, u2, u3, res);
  return true;
}

bool invMapUV(MTriangle *t, double *p, bidimMeshData & data,
              double *uv, double tol)
{
  double mat[2][2];
  double b[2];

  int index0 = data.getIndex (t->getVertex(0)); 
  int index1 = data.getIndex (t->getVertex(1)); 
  int index2 = data.getIndex (t->getVertex(2)); 

  double u0 = data.Us[index0];
  double v0 = data.Vs[index0];
  double u1 = data.Us[index1];
  double v1 = data.Vs[index1];
  double u2 = data.Us[index2];
  double v2 = data.Vs[index2];

  mat[0][0] = u1 - u0;
  mat[0][1] = u2 - u0;
  mat[1][0] = v1 - v0;
  mat[1][1] = v2 - v0;

  b[0] = p[0] - u0;
  b[1] = p[1] - v0;
  sys2x2(mat, b, uv);

  if(uv[0] >= -tol &&
     uv[1] >= -tol &&
     uv[0] <= 1. + tol &&
     uv[1] <= 1. + tol &&
     1. - uv[0] - uv[1] > -tol) {
    return true;
  }
  return false;
}

inline double getSurfUV(MTriangle *t, bidimMeshData & data)
{
  int index0 = data.getIndex (t->getVertex(0)); 
  int index1 = data.getIndex (t->getVertex(1)); 
  int index2 = data.getIndex (t->getVertex(2)); 

  double u1 = data.Us[index0];
  double v1 = data.Vs[index0];
  double u2 = data.Us[index1];
  double v2 = data.Vs[index1];
  double u3 = data.Us[index2];
  double v3 = data.Vs[index2];

  const double vv1[2] = {u2 - u1, v2 - v1};
  const double vv2[2] = {u3 - u1, v3 - v1};
  double s = vv1[0] * vv2[1] - vv1[1] * vv2[0];
  return s * 0.5;
}

bool insertVertexB (std::list<edgeXface> &shell,
		    std::list<MTri3*> &cavity,
		    bool force, GFace *gf, MVertex *v, double *param , MTri3 *t,
		    std::set<MTri3*, compareTri3Ptr> &allTets,
		    std::set<MTri3*, compareTri3Ptr> *activeTets,
		    bidimMeshData & data,
		    double *metric,
		    MTri3 **oneNewTriangle)
{
  if (shell.size() <= 3 || shell.size() != cavity.size() + 2) return false;

  std::list<MTri3*> new_cavity;

  // check that volume is conserved
  double newVolume = 0;
  double oldVolume = 0;

  std::list<MTri3*>::iterator ittet = cavity.begin();
  std::list<MTri3*>::iterator ittete = cavity.end();
  while(ittet != ittete){
    oldVolume += fabs(getSurfUV((*ittet)->tri(),data));
    ++ittet;
  }

  MTri3 **newTris = new MTri3*[shell.size()];
  int k = 0;
  std::list<edgeXface>::iterator it = shell.begin();

  bool onePointIsTooClose = false;
  while (it != shell.end()){
    MTriangle *t = new MTriangle(it->v[0], it->v[1], v);
    int index0 = data.getIndex (t->getVertex(0)); 
    int index1 = data.getIndex (t->getVertex(1)); 
    int index2 = data.getIndex (t->getVertex(2)); 
    const double ONE_THIRD = 1./3.;
    double lc = ONE_THIRD * (data.vSizes[index0] +data.vSizes[index1] +data.vSizes[index2]);
    double lcBGM = ONE_THIRD * (data.vSizesBGM[index0] +data.vSizesBGM[index1] +data.vSizesBGM[index2]);
    double LL = Extend1dMeshIn2dSurfaces() ? std::min(lc, lcBGM) : lcBGM;

    MTri3 *t4;
    t4 = new MTri3(t, LL,0,&data,gf);
    if (oneNewTriangle) {force = true; *oneNewTriangle = t4;}
    //    double din = t->getInnerRadius();

    double d1 = distance(it->v[0],v);
    double d2 = distance(it->v[1],v);
    double d3 = distance(it->v[0],it->v[1]);

    // avoid angles that are too obtuse
    double cosv = ((d1*d1+d2*d2-d3*d3)/(2.*d1*d2));

    if ((d1 < LL * .25 || d2 < LL * .25 || cosv < -.9999) && !force) {
      onePointIsTooClose = true;
      //      printf("%12.5E %12.5E %12.5E %12.5E \n",d1,d2,LL,cosv);
    }

    newTris[k++] = t4;
    // all new triangles are pushed front in order to be able to
    // destroy them if the cavity is not star shaped around the new
    // vertex.
    new_cavity.push_back(t4);
    MTri3 *otherSide = it->t1->getNeigh(it->i1);
    if (otherSide)
      new_cavity.push_back(otherSide);
    double ss = fabs(getSurfUV(t4->tri(), data));
    if (ss < 1.e-25) ss = 1.e22;
    newVolume += ss;
    ++it;
  }


  if (fabs(oldVolume - newVolume) < 1.e-12 * oldVolume && !onePointIsTooClose){
    connectTris_vector(new_cavity.begin(), new_cavity.end());
    //    printf("%d %d\n",shell.size(),cavity.size());
    //    clock_t t1 = clock();
    // 30 % of the time is spent here !!!
    allTets.insert(newTris, newTris + shell.size());
    //    clock_t t2 = clock();
    //    __DT2 += (double)(clock()-t1)/CLOCKS_PER_SEC;
    if (activeTets){
      for (std::list<MTri3*>::iterator i = new_cavity.begin(); i != new_cavity.end(); ++i){
        int active_edge;
        if(isActive(*i, LIMIT_, active_edge) && (*i)->getRadius() > LIMIT_){
          if ((*activeTets).find(*i) == (*activeTets).end())
            (*activeTets).insert(*i);
        }
      }
    }
    delete [] newTris;
    return true;
  }

  // The cavity is NOT star shaped
  else{

    //    printf("(%g %g) %22.15E %22.15E %d %d %d %d %d\n",v->x(),v->y(),oldVolume,  newVolume, shell.size(), onePointIsTooClose, cavity.size(), new_cavity.size(),allTets.size());
    //    printf("12.5E 12.5E 12.5E 12.5E 12.5E\n",d1,d2,LL,cosv);

    ittet = cavity.begin();
    ittete = cavity.end();
    while(ittet != ittete){
      (*ittet)->setDeleted(false);
      ++ittet;
    }
    //    _printTris("cavity.pos", cavity.begin(), cavity.end(), Us, Vs, false);
    //    _printTris("new_cavity.pos", new_cavity.begin(), new_cavity.end(), Us, Vs, false);
    //    _printTris("newTris.pos", &newTris[0], newTris+shell.size(), Us, Vs, false);
    //    _printTris("allTris.pos", allTets.begin(),allTets.end(), Us, Vs, false);
    for (unsigned int i = 0; i < shell.size(); i++) delete newTris[i];
    delete [] newTris;
    //    throw;
    //    double t2 = Cpu();
    //    DT_INSERT_VERTEX += t2-t1;
    return false;
  }
}

bool insertVertex(bool force, GFace *gf, MVertex *v, double *param , MTri3 *t,
                  std::set<MTri3*, compareTri3Ptr> &allTets,
                  std::set<MTri3*, compareTri3Ptr> *activeTets,
		  bidimMeshData & data,
                  double *metric,
		  MTri3 **oneNewTriangle)
{
  std::list<edgeXface> shell;
  std::list<MTri3*> cavity;

  if (!metric){
    double p[3] = {v->x(), v->y(), v->z()};
    recurFindCavity(shell, cavity, p, param, t, data);
  }
  else{
    recurFindCavityAniso(gf, shell, cavity, metric, param, t, data);
  }

  return insertVertexB(shell, cavity, force, gf, v, param , t,
		       allTets,
		       activeTets,
		       data,
		       metric,
		       oneNewTriangle);
}


static MTri3* search4Triangle (MTri3 *t, double pt[2], bidimMeshData & data,
			       std::set<MTri3*,compareTri3Ptr> &AllTris, double uv[2], bool force = false) {

  //  bool inside = t->inCircumCircle(pt);
  bool inside =  invMapUV(t->tri(), pt, data, uv, 1.e-8);



  //  printf("inside = %d (%g %g)\n",inside,pt[0],pt[1]);
  if (inside) return t;
  SPoint3 q1(pt[0],pt[1],0);
  int ITER = 0;
  while (1){
    N_SEARCH ++ ;
    int index0 = data.getIndex (t->tri()->getVertex(0)); 
    int index1 = data.getIndex (t->tri()->getVertex(1)); 
    int index2 = data.getIndex (t->tri()->getVertex(2)); 
    SPoint3 q2((data.Us[index0] +data.Us[index1] +data.Us[index2])/ 3.0,
	       (data.Vs[index0] +data.Vs[index1] +data.Vs[index2])/ 3.0,
	       0);
    int i;
    for (i = 0; i < 3; i++){
      int i1 = data.getIndex (t->tri()->getVertex(i == 0 ? 2 : i-1) );
      int i2 = data.getIndex (t->tri()->getVertex(i) );
      SPoint3 p1 (data.Us[i1],data.Vs[i1],0);
      SPoint3 p2 (data.Us[i2],data.Vs[i2],0);
      double xcc[2];
      if (intersection_segments(p1, p2, q1, q2, xcc)) break;
    }
    if (i >= 3) break;
    t = t->getNeigh(i);
    if (!t) break;
    bool inside = invMapUV(t->tri(), pt, data, uv, 1.e-8);
    //    printf("ITER %d %d\n",ITER,inside);
    if (inside) return t;
    if (ITER++ > (int)AllTris.size()) break;
  }

  if (!force)return 0; // FIXME: removing this leads to horrible performance

  N_GLOBAL_SEARCH ++ ;
  for(std::set<MTri3*,compareTri3Ptr>::iterator itx = AllTris.begin();
      itx != AllTris.end();++itx){
    if (!(*itx)->isDeleted()){
      inside = invMapUV((*itx)->tri(), pt, data, uv, 1.e-8);
      if (inside){
	return *itx;
      }
    }
  }
  return 0;
}

//double __DT1;

static bool insertAPoint(GFace *gf, std::set<MTri3*,compareTri3Ptr>::iterator it,
                         double center[2], double metric[3], bidimMeshData & data,
                         std::set<MTri3*,compareTri3Ptr> &AllTris,
                         std::set<MTri3*,compareTri3Ptr> *ActiveTris = 0,
                         MTri3 *worst = 0,  MTri3 **oneNewTriangle = 0)
{

  if (worst){
    it = AllTris.find(worst);
    if (worst != *it){
      Msg::Error("Could not insert point");
      return false;
    }
  }
  else worst = *it;

  MTri3 *ptin = 0;
  std::list<edgeXface> shell;
  std::list<MTri3*> cavity;
  double uv[2];

  // if the point is able to break the bad triangle "worst"
  if (inCircumCircleAniso(gf, worst->tri(), center, metric, data)){
    //      double t1 = Cpu();
    recurFindCavityAniso(gf, shell, cavity, metric, center, worst, data);
    //      __DT1 += Cpu() - t1 ;
    for (std::list<MTri3*>::iterator itc = cavity.begin(); itc != cavity.end(); ++itc){
      if (invMapUV((*itc)->tri(), center, data, uv, 1.e-8)) {
	ptin = *itc;
	break;
      }
    }
  }
  // else look for it
  else {
    //      printf("cocuou\n");
    ptin = search4Triangle (worst, center, data, AllTris,uv, oneNewTriangle ? true : false);
      if (ptin) {
	recurFindCavityAniso(gf, shell, cavity, metric, center, ptin, data);    
      }
  }

  if (ptin) {
    // we use here local coordinates as real coordinates
    // x,y and z will be computed hereafter
    // Msg::Info("Point is inside");
    GPoint p = gf->point(center[0], center[1]);

    // should not have an omp critical here
    MVertex *v = new MFaceVertex(p.x(), p.y(), p.z(), gf, center[0], center[1]);

    double lc1,lc;
    int index0 = data.getIndex(ptin->tri()->getVertex(0));
    int index1 = data.getIndex(ptin->tri()->getVertex(1));
    int index2 = data.getIndex(ptin->tri()->getVertex(2));
    lc1 = (1. - uv[0] - uv[1]) * data.vSizes[index0] + uv[0] * data.vSizes[index1] + uv[1] * data.vSizes[index2];
    lc = BGM_MeshSize(gf, center[0], center[1], p.x(), p.y(), p.z());
    
    //SMetric3 metr = BGM_MeshMetric(gf, center[0], center[1], p.x(), p.y(), p.z());
    //                               vMetricsBGM.push_back(metr);
    data.addVertex ( v ,  center[0], center[1], lc1, lc ); 
    
    //    double t1 = Cpu();

    if(!p.succeeded() || !insertVertexB
       (shell, cavity,false, gf, v, center, ptin, AllTris,ActiveTris, data , metric, oneNewTriangle) ) {
      Msg::Debug("Point %g %g cannot be inserted because %d",
		 center[0], center[1], p.succeeded() );
      //      printf("Point %g %g cannot be inserted because %d",
      //	     center[0], center[1], p.succeeded() );
      AllTris.erase(it);
      worst->forceRadius(-1);
      AllTris.insert(worst);
      delete v;
      for (std::list<MTri3*>::iterator itc = cavity.begin(); itc != cavity.end(); ++itc)(*itc)->setDeleted(false);
      return false;
    }
    else {
      //      printf("done ! %d\n",AllTris.size());
      //      double t2 = Cpu();
      //      DT_INSERT_VERTEX += (t2-t1);
      gf->mesh_vertices.push_back(v);
      return true;
    }
  }
  else {
    //    MTriangle *base = worst->tri();
    for (std::list<MTri3*>::iterator itc = cavity.begin(); itc != cavity.end(); ++itc)(*itc)->setDeleted(false);
    AllTris.erase(it);
    worst->forceRadius(0);
    AllTris.insert(worst);
    return false;
  }
}

void bowyerWatson(GFace *gf, int MAXPNT,
		  std::map<MVertex* , MVertex*>* equivalence,  
		  std::map<MVertex*, SPoint2> * parametricCoordinates)
{
  std::set<MTri3*,compareTri3Ptr> AllTris;
  bidimMeshData DATA(equivalence,parametricCoordinates);

  buildMeshGenerationDataStructures(gf, AllTris, DATA);

  //  if (equivalence)_printTris ("before.pos", AllTris.begin(), AllTris.end(), DATA);
  int nbSwaps = edgeSwapPass(gf, AllTris, SWCR_DEL, DATA);
  // _printTris ("after2.pos", AllTris, Us,Vs);
  Msg::Debug("Delaunization of the initial mesh done (%d swaps)", nbSwaps);

  if(AllTris.empty()){
    Msg::Error("No triangles in initial mesh");
    return;
  }

  int ITER = 0;
  int NBDELETED = 0;
  //  double DT1 = 0 , DT2=0, DT3=0;
  while (1){
    //    if(ITER % 1== 0){
    //      char name[245];
    //      sprintf(name,"del2d%d-ITER%4d.pos",gf->tag(),ITER);
    //      _printTris (name, AllTris, Us,Vs,false);
    //    }
    MTri3 *worst = *AllTris.begin();
    if (worst->isDeleted()){
      //      double t1 = Cpu();
      delete worst->tri();
      delete worst;
      AllTris.erase(AllTris.begin());
      NBDELETED ++;
      //      DT1 += (Cpu() - t1);
    }
    else{
      //      double t2 = Cpu();
      if(ITER++ % 5000 == 0){
        Msg::Debug("%7d points created -- Worst tri radius is %8.3f",
                   DATA.vSizes.size(), worst->getRadius());
	//	printf("%d %d %d\n",vSizes.size(), AllTris.size(),NBDELETED);
      }
      double center[2],metric[3],r2;
      if (worst->getRadius() < 0.5 * sqrt(2.0) || (int) DATA.vSizes.size() > MAXPNT) break;
      circUV(worst->tri(), DATA, center, gf);
      MTriangle *base = worst->tri();
      int index0 = DATA.getIndex( base->getVertex(0) );
      int index1 = DATA.getIndex( base->getVertex(1) );
      int index2 = DATA.getIndex( base->getVertex(2) );
      double pa[2] = {(DATA.Us[index0] + DATA.Us[index1] + DATA.Us[index2])/ 3.,
                      (DATA.Vs[index0] + DATA.Vs[index1] + DATA.Vs[index2])/ 3.};
      buildMetric(gf, pa,  metric);
      circumCenterMetric(worst->tri(), metric, DATA, center, r2);
      //      DT2 += (Cpu() - t2) ;
      //      double t3 = Cpu() ;
      insertAPoint(gf, AllTris.begin(), center, metric, DATA, AllTris);
      //      DT3 += (Cpu() - t3) ;
    }
  }
  //  printf("%12.5E %12.5E %12.5E %12.5E %12.5E\n",DT1,DT2,DT3,__DT1,__DT2);
  //  printf("%12.5E \n",__DT2);
#if defined(HAVE_ANN)
  {
    FieldManager *fields = gf->model()->getFields();
    BoundaryLayerField *blf = 0;
    if(fields->getBoundaryLayerField() > 0){
      Field *bl_field = fields->get(fields->getBoundaryLayerField());
      blf = dynamic_cast<BoundaryLayerField*> (bl_field);
      if (blf && !blf->iRecombine) quadsToTriangles(gf,10000);
    }
  }
#endif
  transferDataStructure(gf, AllTris, DATA);  
}

/*
  Let's try a frontal delaunay approach now that the delaunay mesher is stable.
  We use the approach of Rebay (JCP 1993) that we extend to anisotropic metrics.
  The point isertion is done on the Voronoi Edges.
*/

double lengthInfniteNorm(const double p[2], const double q[2],
			 const double quadAngle){
  double xp =  p[0] * cos(quadAngle) + p[1] * sin(quadAngle);
  double yp = -p[0] * sin(quadAngle) + p[1] * cos(quadAngle);
  double xq =  q[0] * cos(quadAngle) + q[1] * sin(quadAngle);
  double yq = -q[0] * sin(quadAngle) + q[1] * cos(quadAngle);
  double xmax = std::max(xp,xq);
  double xmin = std::min(xp,xq);
  double ymax = std::max(yp,yq);
  double ymin = std::min(yp,yq);
  return std::max(xmax-xmin,ymax-ymin);
}

void circumCenterInfinite (MTriangle *base, double quadAngle,bidimMeshData & data, double *x)
{
  int index0 = data.getIndex(base->getVertex(0));
  int index1 = data.getIndex(base->getVertex(1));
  int index2 = data.getIndex(base->getVertex(2));
  double pa[2] = {data.Us[index0],data.Vs[index0]};
  double pb[2] = {data.Us[index1],data.Vs[index1]};
  double pc[2] = {data.Us[index2],data.Vs[index2]};
  double xa =  pa[0] * cos(quadAngle) - pa[1] * sin(quadAngle);
  double ya =  pa[0] * sin(quadAngle) + pa[1] * cos(quadAngle);
  double xb =  pb[0] * cos(quadAngle) - pb[1] * sin(quadAngle);
  double yb =  pb[0] * sin(quadAngle) + pb[1] * cos(quadAngle);
  double xc =  pc[0] * cos(quadAngle) - pc[1] * sin(quadAngle);
  double yc =  pc[0] * sin(quadAngle) + pc[1] * cos(quadAngle);
  double xmax = std::max(std::max(xa,xb),xc);
  double ymax = std::max(std::max(ya,yb),yc);
  double xmin = std::min(std::min(xa,xb),xc);
  double ymin = std::min(std::min(ya,yb),yc);
  x[0] = 0.5 * (xmax-xmin);
  x[1] = 0.5 * (ymax-ymin);
}

static double lengthMetric(const double p[2], const double q[2],
                           const double metric[3])
{
  return sqrt (     (p[0] - q[0]) * metric[0] * (p[0] - q[0]) +
                2 * (p[0] - q[0]) * metric[1] * (p[1] - q[1]) +
                    (p[1] - q[1]) * metric[2] * (p[1] - q[1]) );
}

/*
          /|
         / |
        /  |
       /   |
   lc /    |  r
     /     |
    /      |
   /       x
  /        |
 /         |  r/2
/          |
-----------+
     lc/2

     (3 r/2)^2 = lc^2 - lc^2/4
     -> lc^2 3/4 = 9r^2/4
     -> lc^2 = 3 r^2

     r^2 /4 + lc^2/4 = r^2
     -> lc^2 = 3 r^2

*/

double optimalPointFrontal (GFace *gf,
			    MTri3* worst,
			    int active_edge,
			    bidimMeshData & data,
			    double newPoint[2],
			    double metric[3])
{
  double center[2],r2;
  MTriangle *base = worst->tri();
  circUV(base, data, center, gf);
  int index0 = data.getIndex(base->getVertex(0));
  int index1 = data.getIndex(base->getVertex(1));
  int index2 = data.getIndex(base->getVertex(2));
  double pa[2] = {(data.Us[index0] + data.Us[index1] + data.Us[index2])/ 3.,
		  (data.Vs[index0] + data.Vs[index1] + data.Vs[index2])/ 3.};
  buildMetric(gf, pa, metric);
  circumCenterMetric(worst->tri(), metric, data, center, r2);
  // compute the middle point of the edge
  int ip1 = active_edge - 1 < 0 ? 2 : active_edge - 1;
  int ip2 = active_edge;

  index0 = data.getIndex(base->getVertex(ip1));
  index1 = data.getIndex(base->getVertex(ip2));
  double P[2] =  {data.Us[index0],data.Vs[index0]};
  double Q[2] =  {data.Us[index1],data.Vs[index1]};
  double midpoint[2] = {0.5 * (P[0] + Q[0]), 0.5 * (P[1] + Q[1])};

  // now we have the edge center and the center of the circumcircle,
  // we try to find a point that would produce a perfect triangle while
  // connecting the 2 points of the active edge

  double dir[2] = {center[0] - midpoint[0], center[1] - midpoint[1]};
  double norm = sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
  dir[0] /= norm;
  dir[1] /= norm;
  const double RATIO = sqrt(dir[0] * dir[0] * metric[0] +
			    2 * dir[1] * dir[0] * metric[1] +
			    dir[1] * dir[1] * metric[2]);

  const double rhoM1 = 0.5* (data.vSizes[index0] +data.vSizes[index1] ) ;// * RATIO;
  const double rhoM2 = 0.5* (data.vSizesBGM[index0] +data.vSizesBGM[index1] ) ;// * RATIO;
  const double rhoM  = Extend1dMeshIn2dSurfaces() ? std::min(rhoM1,rhoM2) : rhoM2;
  const double rhoM_hat = rhoM;

  const double q = lengthMetric(center, midpoint, metric);
  const double d = rhoM_hat * sqrt(3.)*0.5;

  // d is corrected in a way that the mesh size is computed at point newPoint


  //  printf("%12.5E %12.5E\n",d,RATIO);

  //  const double L = d ;
  // avoid to go toooooo far
  const double L = d > q ? q : d;


  newPoint[0] = midpoint[0] + L * dir[0]/RATIO;
  newPoint[1] = midpoint[1] + L * dir[1]/RATIO;


  return L;// > q ? d : q;
}

/*
            x
            |
            |
            | d =  3^{1/2}/2 h
            |
            |
      ------p------->   n
            h

   x point of the plane

   h being some kind of average between the size field
   and the edge length
*/

void optimalPointFrontalB (GFace *gf,
			   MTri3* worst,
			   int active_edge,
			   bidimMeshData & data,
			   double newPoint[2],
			   double metric[3])
{
  // as a starting point, let us use the "fast algo"
  double d = optimalPointFrontal (gf,worst,active_edge,data,newPoint,metric);
  int ip1 = active_edge - 1 < 0 ? 2 : active_edge - 1;
  int ip2 = active_edge;
  MVertex *v1 = worst->tri()->getVertex(ip1);
  MVertex *v2 = worst->tri()->getVertex(ip2);
  MTriangle *t = worst->tri();
  double p1[3] = {t->getVertex(0)->x(), t->getVertex(0)->y(), t->getVertex(0)->z()};
  double p2[3] = {t->getVertex(1)->x(), t->getVertex(1)->y(), t->getVertex(1)->z()};
  double p3[3] = {t->getVertex(2)->x(), t->getVertex(2)->y(), t->getVertex(2)->z()};
  double c[3];
  circumCenterXYZ(p1, p2, p3, c);
  SVector3 middle ((v1->x()+v2->x())*.5,(v1->y()+v2->y())*.5,(v1->z()+v2->z())*.5);
  SVector3 center(c[0],c[1],c[2]);
  SVector3 v1v2 (v2->x()-v1->x(),v2->y()-v1->y(),v2->z()-v1->z());
  SVector3 n1 = center - middle;
  SVector3 n2 = crossprod(v1v2,n1);
  n1.normalize();
  n2.normalize();
  // we look for a point that is
  // P = d * (n1 cos(t) + n2 sin(t)) that is on the surface
  // so we have to find t, starting with t = 0

  if (gf->geomType() == GEntity::CompoundSurface){
    GFaceCompound *gfc = dynamic_cast<GFaceCompound*> (gf);
    if (gfc){
      GPoint gp = gfc->intersectionWithCircle(n1,n2,middle,d,newPoint);
      if (gp.succeeded()){
	newPoint[0] = gp.u();
	newPoint[1] = gp.v();
	return ;
      }
    }
  }

  double uvt[3] = {newPoint[0],newPoint[1],0.0};
  curveFunctorCircle cc (n1,n2,middle,d);
  surfaceFunctorGFace ss (gf);

  if (intersectCurveSurface (cc,ss,uvt,d*1.e-8)){
    newPoint[0] = uvt[0];
    newPoint[1] = uvt[1];
  }
  else {
    Msg::Debug("--- Non optimal point found -----------");
    //    Msg::Info("--- Non optimal point found -----------");
  }
}

void bowyerWatsonFrontal(GFace *gf,
		  std::map<MVertex* , MVertex*>* equivalence,  
		  std::map<MVertex*, SPoint2> * parametricCoordinates)
{
  std::set<MTri3*,compareTri3Ptr> AllTris;
  std::set<MTri3*,compareTri3Ptr> ActiveTris;
  bidimMeshData DATA(equivalence,parametricCoordinates);

  buildMeshGenerationDataStructures(gf, AllTris, DATA);

  // delaunise the initial mesh
  int nbSwaps = edgeSwapPass(gf, AllTris, SWCR_DEL, DATA);
  Msg::Debug("Delaunization of the initial mesh done (%d swaps)", nbSwaps);

  int ITER = 0, active_edge;
  // compute active triangle
  std::set<MTri3*,compareTri3Ptr>::iterator it = AllTris.begin();
  for ( ; it!=AllTris.end();++it){
    if(isActive(*it,LIMIT_,active_edge))
      ActiveTris.insert(*it);
    else if ((*it)->getRadius() < LIMIT_)break;
  }

  // insert points
  int ITERATION = 0;
  while (1){
    ++ITERATION;
    if(ITERATION % 10== 0 && CTX::instance()->mesh.saveAll){
      char name[245];
      sprintf(name,"delFrontal_GFace_%d_Layer_%d.pos",gf->tag(),ITERATION);
      _printTris (name, AllTris.begin(), AllTris.end(), DATA,true);
      sprintf(name,"delFrontal_GFace_%d_Layer_%d_Active.pos",gf->tag(),ITERATION);
      _printTris (name, ActiveTris.begin(), ActiveTris.end(), DATA,true);
    }
    /* if(ITER % 100== 0){
          char name[245];
          sprintf(name,"delfr2d%d-ITER%d.pos",gf->tag(),ITER);
          _printTris (name, AllTris, Us,Vs,true);
	  //          sprintf(name,"delfr2dA%d-ITER%d.pos",gf->tag(),ITER);
	  //          _printTris (name, ActiveTris, Us,Vs,false);
        }
    */
    if (!ActiveTris.size())break;
    MTri3 *worst = (*ActiveTris.begin());
    ActiveTris.erase(ActiveTris.begin());
    // printf("active_tris.size = %d\n",ActiveTris.size());

    if (!worst->isDeleted() && isActive(worst, LIMIT_, active_edge) &&
        worst->getRadius() > LIMIT_){
      if(ITER++ % 5000 == 0)
        Msg::Debug("%7d points created -- Worst tri radius is %8.3f",
                   gf->mesh_vertices.size(), worst->getRadius());
      double newPoint[2], metric[3];
      //optimalPointFrontal (gf,worst,active_edge,Us,Vs,vSizes,vSizesBGM,newPoint,metric);
      optimalPointFrontalB (gf,worst,active_edge,DATA,newPoint,metric);
      insertAPoint(gf, AllTris.end(), newPoint, metric, DATA, AllTris, &ActiveTris, worst);
    }

    /*   if(ITER % 1== 0){
       char name[245];
       sprintf(name,"frontal%d-ITER%d.pos",gf->tag(),ITER);
       _printTris (name, AllTris, Us,Vs,false);
     }
    */
  }

  // char name[245];
  // sprintf(name,"frontal%d-real.pos", gf->tag());
  // _printTris (name, AllTris, Us, Vs,false);
  // sprintf(name,"frontal%d-param.pos", gf->tag());
  // _printTris (name, AllTris, Us, Vs,true);
  transferDataStructure(gf, AllTris, DATA);
  // in case of boundary layer meshing
#if defined(HAVE_ANN)
  {
    FieldManager *fields = gf->model()->getFields();
    BoundaryLayerField *blf = 0;
    if(fields->getBoundaryLayerField() > 0){
      Field *bl_field = fields->get(fields->getBoundaryLayerField());
      blf = dynamic_cast<BoundaryLayerField*> (bl_field);
      if (blf && !blf->iRecombine)quadsToTriangles(gf,10000);
    }
  }
#endif
}

void optimalPointFrontalQuad (GFace *gf,
			      MTri3* worst,
			      int active_edge,
			      bidimMeshData &data,
			      double newPoint[2],
			      double metric[3])
{
  MTriangle *base = worst->tri();
  int ip1 = active_edge - 1 < 0 ? 2 : active_edge - 1;
  int ip2 = active_edge;
  int ip3 = (active_edge+1)%3;

  int index1 = data.getIndex(base->getVertex(ip1));
  int index2 = data.getIndex(base->getVertex(ip2));
  int index3 = data.getIndex(base->getVertex(ip3));

  double P[2] =  {data.Us[index1],data.Vs[index1]};
  double Q[2] =  {data.Us[index2],data.Vs[index2]};
  double O[2] =  {data.Us[index3],data.Vs[index3]};
  double midpoint[2] = {0.5 * (P[0] + Q[0]), 0.5 * (P[1] + Q[1])};

  // compute background mesh data
  double quadAngle  = backgroundMesh::current()->getAngle (midpoint[0],midpoint[1],0);
  double center[2];
  circumCenterInfinite (base, quadAngle,data,center);

  // rotate the points with respect to the angle
  double XP1 = 0.5*(Q[0] - P[0]);
  double YP1 = 0.5*(Q[1] - P[1]);
  double xp =  XP1 * cos(quadAngle) + YP1 * sin(quadAngle);
  double yp = -XP1 * sin(quadAngle) + YP1 * cos(quadAngle);
  // ensure xp > yp
  bool exchange = false;
  if (fabs(xp) < fabs(yp)){
    double temp = xp;
    xp = yp;
    yp = temp;
    exchange = true;
  }

  buildMetric(gf, midpoint, metric);
  double RATIO = 1./pow(metric[0]*metric[2]-metric[1]*metric[1],0.25);

  const double p = 0.5 * lengthInfniteNorm(P, Q, quadAngle);
  const double q = lengthInfniteNorm(center, midpoint, quadAngle);

  const double rhoM1 = 0.5*RATIO* (data.vSizes[index1] +data.vSizes[index2] ) / sqrt(3.);
  const double rhoM2 = 0.5*RATIO* (data.vSizesBGM[index1] +data.vSizesBGM[index2] ) / sqrt(3.);
  const double rhoM  = Extend1dMeshIn2dSurfaces() ? std::min(rhoM1, rhoM2) : rhoM2;

  const double rhoM_hat = std::min(std::max(rhoM, p), (p * p + q * q) / (2 * q));
  const double factor = (rhoM_hat + sqrt (rhoM_hat * rhoM_hat - p * p)) /(sqrt(3.)*p);

  double npx,npy;
  if (xp*yp >  0){
    npx = - fabs(xp)*factor;
    npy = fabs(xp)*(1.+factor) - fabs(yp);
  }
  else {
    npx = fabs(xp) * factor;
    npy = (1.+factor)*fabs(xp) - fabs(yp);
  }
  if (exchange){
    double temp = npx;
    npx = npy;
    npy = temp;
  }

  newPoint[0] = midpoint[0] + cos(quadAngle) * npx - sin(quadAngle) * npy;
  newPoint[1] = midpoint[1] + sin(quadAngle) * npx + cos(quadAngle) * npy;

  if ((midpoint[0] - newPoint[0])*(midpoint[0] - O[0]) +
      (midpoint[1] - newPoint[1])*(midpoint[1] - O[1]) < 0){
    newPoint[0] = midpoint[0] - cos(quadAngle) * npx + sin(quadAngle) * npy;
    newPoint[1] = midpoint[1] - sin(quadAngle) * npx - cos(quadAngle) * npy;
  }
}

void optimalPointFrontalQuadB (GFace *gf,
			       MTri3* worst,
			       int active_edge,
			       bidimMeshData &data,
			       double newPoint[2],
			       double metric[3])
{
  optimalPointFrontalQuad (gf,worst,active_edge,data,newPoint,metric);
  return;
}

void buildBackGroundMesh (GFace *gf,
		  std::map<MVertex* , MVertex*>* equivalence,  
		  std::map<MVertex*, SPoint2> * parametricCoordinates)
{
  //  printf("build bak mesh\n");
  quadsToTriangles(gf, 100000);

  if (!backgroundMesh::current()) {
    std::vector<MTriangle*> TR;
    //    std::vector<MQuadrangle*> QR;
    for(unsigned int i = 0; i < gf->triangles.size(); i++){
      TR.push_back(new MTriangle(gf->triangles[i]->getVertex(0),
				 gf->triangles[i]->getVertex(1),
				 gf->triangles[i]->getVertex(2)));
    }
    /*
    for(int i=0;i<gf->quadrangles.size();i++){
      QR.push_back(new MQuadrangle(gf->quadrangles[i]->getVertex(0),
				   gf->quadrangles[i]->getVertex(1),
				   gf->quadrangles[i]->getVertex(2),
				   gf->quadrangles[i]->getVertex(3)));
    }
    */
    // avoid computing curvatures on the fly : only on the
    // BGM computes once curvatures at each node
    //  Disable curvature control
    int CurvControl = CTX::instance()->mesh.lcFromCurvature;
    CTX::instance()->mesh.lcFromCurvature = 0;
    //  Do a background mesh
    bowyerWatson(gf,4000, equivalence,parametricCoordinates);
    //  Re-enable curv control if asked
    CTX::instance()->mesh.lcFromCurvature = CurvControl;
    // apply this to the BGM
    //    printf("1 end build bak mesh\n");
    backgroundMesh::set(gf);
    //    printf("2 end build bak mesh\n");
    char name[256];
    if (CTX::instance()->mesh.saveAll){
      sprintf(name,"bgm-%d.pos",gf->tag());
      backgroundMesh::current()->print(name,gf);
      sprintf(name,"cross-%d.pos",gf->tag());
      backgroundMesh::current()->print(name,gf,1);
    }
    gf->triangles = TR;
    //    gf->quadrangles = QR;
  }

}

void bowyerWatsonFrontalLayers(GFace *gf, bool quad,
		  std::map<MVertex* , MVertex*>* equivalence,  
		  std::map<MVertex*, SPoint2> * parametricCoordinates)
{

  std::set<MTri3*,compareTri3Ptr> AllTris;
  std::set<MTri3*,compareTri3Ptr> ActiveTris;
  bidimMeshData DATA(equivalence, parametricCoordinates);

  if (quad){
    LIMIT_ = sqrt(2.) * .99;
    //LIMIT_ = 4./3.;//sqrt(2.) * .99;
    MTri3::radiusNorm =-1;
  }

  buildMeshGenerationDataStructures (gf, AllTris, DATA);

  // delaunise the initial mesh
  int nbSwaps = edgeSwapPass(gf, AllTris, SWCR_DEL, DATA);
  Msg::Debug("Delaunization of the initial mesh done (%d swaps)", nbSwaps);

  int ITER = 0, active_edge;
  // compute active triangle
  std::set<MTri3*,compareTri3Ptr>::iterator it = AllTris.begin();
  std::set<MEdge,Less_Edge> _front;
  for ( ; it!=AllTris.end();++it){
    if(isActive(*it,LIMIT_,active_edge)){
      ActiveTris.insert(*it);
      updateActiveEdges(*it, LIMIT_, _front);
    }
    else if ((*it)->getRadius() < LIMIT_)break;
  }

  // insert points
  int ITERATION = 1;
  int max_layers = quad ? 10000 : 4;
  while (1){
    ITERATION ++;
    if(ITERATION % 1== 0 && CTX::instance()->mesh.saveAll){
      char name[245];
      sprintf(name,"delInfinite_GFace_%d_Layer_%d.pos",gf->tag(),ITERATION);
      _printTris (name, AllTris.begin(), AllTris.end(), DATA,true);
      sprintf(name,"delInfinite_GFace_%d_Layer_%d_Active.pos",gf->tag(),ITERATION);
      _printTris (name, ActiveTris.begin(),  ActiveTris.end(),DATA,true);
    }

    std::set<MTri3*,compareTri3Ptr> ActiveTrisNotInFront;

    //    printf("%d active triangles\n",ActiveTris.size());

    while (1){

      if (!ActiveTris.size())break;

      /*      if (1 || gf->tag() == 1900){
	char name[245];
	sprintf(name,"x_GFace_%d_Layer_%d.pos",gf->tag(),ITER);
	_printTris (name, AllTris, Us,Vs,true);
      }
      */
      std::set<MTri3*,compareTri3Ptr>::iterator WORST_ITER = ActiveTris.begin();

      MTri3 *worst = (*WORST_ITER);
      ActiveTris.erase(WORST_ITER);
      if (!worst->isDeleted() &&
          (ITERATION > max_layers ? isActive(worst, LIMIT_, active_edge) :
           isActive(worst, LIMIT_, active_edge,&_front) ) &&
	  worst->getRadius() > LIMIT_){
	// for (active_edge = 0 ; active_edge < 0 ; active_edge ++){
	//   if (active_edges[active_edge])break;
	// }
	// Msg::Info("%7d points created -- Worst tri infinite radius is %8.3f -- "
        //           "front size %6d", vSizes.size(), worst->getRadius(),_front.size());
	if(ITER++ % 5000 == 0)
	  Msg::Debug("%7d points created -- Worst tri infinite radius is %8.3f -- "
                     "front size %6d", DATA.vSizes.size(), worst->getRadius(),_front.size());

	// compute the middle point of the edge
	double newPoint[2],metric[3]={1,0,1};
	if (quad) optimalPointFrontalQuadB (gf,worst,active_edge,DATA,newPoint,metric);
	else optimalPointFrontalB (gf,worst,active_edge,DATA,newPoint,metric);

	//	printf("start INSERT A POINT %g %g \n",newPoint[0],newPoint[1]);
	insertAPoint(gf, AllTris.end(), newPoint, 0, DATA, AllTris, &ActiveTris, worst);
	//  else if (!worst->isDeleted() && worst->getRadius() > LIMIT_){
	//    ActiveTrisNotInFront.insert(worst);
	//  }
	//  printf("-----------------> size %d\n",AllTris.size());

	/*
	  if(ITER % 1== 0){
	  char name[245];
	  sprintf(name,"frontal%d-ITER%d.pos",gf->tag(),ITER);
	  _printTris (name, AllTris, Us,Vs,false);
	  }
	*/
      }
      else if (!worst->isDeleted() && worst->getRadius() > LIMIT_){
	ActiveTrisNotInFront.insert(worst);
      }
    }
    _front.clear();
    it = ActiveTrisNotInFront.begin();
    //it = AllTris.begin();
    for ( ; it!=ActiveTrisNotInFront.end();++it){
      //    for ( ; it!=AllTris.end();++it){
      if((*it)->getRadius() > LIMIT_ && isActive(*it,LIMIT_,active_edge)){
	ActiveTris.insert(*it);
	updateActiveEdges(*it, LIMIT_, _front);
      }
    }
    //	Msg::Info("%d active tris %d front edges %d not in front",
    //            ActiveTris.size(),_front.size(),ActiveTrisNotInFront.size());
    if (!ActiveTris.size()) break;
  }

  // char name[245];
  // sprintf(name,"frontal%d-real.pos", gf->tag());
  // _printTris (name, AllTris, Us, Vs,false);
  // sprintf(name,"frontal%d-param.pos", gf->tag());
  // _printTris (name, AllTris, Us, Vs,true);
  transferDataStructure(gf, AllTris, DATA);
  MTri3::radiusNorm = 2;
  LIMIT_ = 0.5 * sqrt(2.0) * 1;
  backgroundMesh::unset();
}

void bowyerWatsonParallelograms(GFace *gf,
		  std::map<MVertex* , MVertex*>* equivalence,  
		  std::map<MVertex*, SPoint2> * parametricCoordinates)
{
  std::set<MTri3*,compareTri3Ptr> AllTris;
  bidimMeshData DATA(equivalence, parametricCoordinates);
  std::vector<MVertex*> packed;
  std::vector<SMetric3> metrics;

  //  printf("creating the points\n");
  packingOfParallelograms(gf, packed, metrics);
  //  printf("points created\n");

  buildMeshGenerationDataStructures (gf, AllTris, DATA);

  // delaunise the initial mesh
  int nbSwaps = edgeSwapPass(gf, AllTris, SWCR_DEL, DATA);
  Msg::Debug("Delaunization of the initial mesh done (%d swaps)", nbSwaps);

  std::sort(packed.begin(), packed.end(), MVertexLessThanLexicographic());

  //  printf("staring to insert points\n");
  N_GLOBAL_SEARCH = 0;
  N_SEARCH = 0;
  DT_INSERT_VERTEX = 0;
  // double t1 = Cpu();
  MTri3 *oneNewTriangle = 0;
  for (unsigned int i=0;i<packed.size();){
    MTri3 *worst = *AllTris.begin();
    if (worst->isDeleted()){
      delete worst->tri();
      delete worst;
      AllTris.erase(AllTris.begin());
    }
    else{
      double newPoint[2] ;
      packed[i]->getParameter(0,newPoint[0]);
      packed[i]->getParameter(1,newPoint[1]);
      delete packed[i];
      double metric[3];
      //      buildMetric(gf, newPoint, metrics[i], metric);
      buildMetric(gf, newPoint, metric);

      bool success = insertAPoint( gf, AllTris.begin(), newPoint, metric, DATA , AllTris, 0, oneNewTriangle, &oneNewTriangle);
      if (!success) oneNewTriangle = 0;
	//      if (!success)printf("success %d %d\n",success,AllTris.size());
      i++;
    }

    if(1.0* AllTris.size() > 2.5 * DATA.vSizes.size()){
      //      int n1 = AllTris.size();
      std::set<MTri3*,compareTri3Ptr>::iterator itd = AllTris.begin();
      while(itd != AllTris.end()){
        if((*itd)->isDeleted()){
          delete  *itd;
          AllTris.erase(itd++);
        }
        else
          itd++;
      }
      //      Msg::Info("cleaning up the memory %d -> %d", n1, AllTris.size());
    }


  }
  //  printf("%d vertices \n",(int)packed.size());
  //clock_t t2 = clock();
  //double DT = (double)(t2-t1)/CLOCKS_PER_SEC;
  //if (packed.size())printf("points inserted DT %12.5E points per minut : %12.5E %d global searchs %d seachs per insertion\n",DT,60.*packed.size()/DT,N_GLOBAL_SEARCH,N_SEARCH / packed.size());
  transferDataStructure(gf, AllTris, DATA);
  backgroundMesh::unset();
}
