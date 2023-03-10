// Gmsh - Copyright (C) 1997-2013 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@geuz.org>.

#include "GmshMessage.h"
#include "BackgroundMesh.h"
#include "Numeric.h"
#include "Context.h"
#include "GVertex.h"
#include "GEdge.h"
#include "GEdgeCompound.h"
#include "GFace.h"
#include "GFaceCompound.h"
#include "GModel.h"
#include "Field.h"
#include "MElement.h"
#include "MElementOctree.h"
#include "MLine.h"
#include "MTriangle.h"
#include "MQuadrangle.h"
#include "MVertex.h"

#if defined(HAVE_SOLVER)
#include "dofManager.h"
#include "laplaceTerm.h"
#include "linearSystemGMM.h"
#include "linearSystemCSR.h"
#include "linearSystemFull.h"
#include "linearSystemPETSc.h"
#endif

// computes the characteristic length of the mesh at a vertex in order
// to have the geometry captured with accuracy. A parameter called
// CTX::instance()->mesh.minCircPoints tells the minimum number of points per
// radius of curvature

#if defined(HAVE_ANN)
static int _NBANN = 2;
#endif

SMetric3 buildMetricTangentToCurve(SVector3 &t, double l_t, double l_n)
{
  if (l_t == 0.0) return SMetric3(1.e-22);
  SVector3 a;
  if (fabs(t(0)) <= fabs(t(1)) && fabs(t(0)) <= fabs(t(2))){
    a = SVector3(1,0,0);
  }
  else if (fabs(t(1)) <= fabs(t(0)) && fabs(t(1)) <= fabs(t(2))){
    a = SVector3(0,1,0);
  }
  else{
    a = SVector3(0,0,1);
  }
  SVector3 b = crossprod (t,a);
  SVector3 c = crossprod (b,t);
  b.normalize();
  c.normalize();
  t.normalize();
  SMetric3 Metric (1./(l_t*l_t),1./(l_n*l_n),1./(l_n*l_n),t,b,c);
  //  printf("bmttc %g %g %g %g %g\n",l_t,l_n,Metric(0,0),Metric(0,1),Metric(1,1));
  return Metric;
}

SMetric3 buildMetricTangentToSurface(SVector3 &t1, SVector3 &t2,
                                     double l_t1, double l_t2, double l_n)
{
  t1.normalize();
  t2.normalize();
  SVector3 n = crossprod (t1,t2);
  n.normalize();

  l_t1 = std::max(l_t1, CTX::instance()->mesh.lcMin);
  l_t2 = std::max(l_t2, CTX::instance()->mesh.lcMin);
  l_t1 = std::min(l_t1, CTX::instance()->mesh.lcMax);
  l_t2 = std::min(l_t2, CTX::instance()->mesh.lcMax);
  SMetric3 Metric (1./(l_t1*l_t1),1./(l_t2*l_t2),1./(l_n*l_n),t1,t2,n);
  return Metric;
}

SMetric3 max_edge_curvature_metric(const GVertex *gv)
{
  SMetric3 val (1.e-12);
  std::list<GEdge*> l_edges = gv->edges();
  for (std::list<GEdge*>::const_iterator ite = l_edges.begin();
       ite != l_edges.end(); ++ite){
    GEdge *_myGEdge = *ite;
    Range<double> range = _myGEdge->parBounds(0);
    SMetric3 cc;
    if (gv == _myGEdge->getBeginVertex()) {
      SVector3 t = _myGEdge->firstDer(range.low());
      t.normalize();
      double l_t = ((2 * M_PI) /( fabs(_myGEdge->curvature(range.low()))
				*  CTX::instance()->mesh.minCircPoints ));
      double l_n = 1.e12;
      cc = buildMetricTangentToCurve(t,l_t,l_n);
    }
    else {
      SVector3 t = _myGEdge->firstDer(range.high());
      t.normalize();
      double l_t = ((2 * M_PI) /( fabs(_myGEdge->curvature(range.high()))
				  *  CTX::instance()->mesh.minCircPoints ));
      double l_n = 1.e12;
      cc = buildMetricTangentToCurve(t,l_t,l_n);
    }
    val = intersection(val,cc);
  }
  return val;
}

SMetric3 max_edge_curvature_metric(const GEdge *ge, double u)
{
  SVector3 t =  ge->firstDer(u);
  t.normalize();
  double l_t = ((2 * M_PI) /( fabs(ge->curvature(u))
			      *  CTX::instance()->mesh.minCircPoints ));
  double l_n = 1.e12;
  return buildMetricTangentToCurve(t,l_t,l_n);
}

static double max_edge_curvature(const GVertex *gv)
{
  double val = 0;
  std::list<GEdge*> l_edges = gv->edges();
  for (std::list<GEdge*>::const_iterator ite = l_edges.begin();
       ite != l_edges.end(); ++ite){
    GEdge *_myGEdge = *ite;
    Range<double> range = _myGEdge->parBounds(0);
    double cc;
    if (gv == _myGEdge->getBeginVertex()) cc = _myGEdge->curvature(range.low());
    else cc = _myGEdge->curvature(range.high());
    val = std::max(val, cc);
  }
  return val;
}

static double max_surf_curvature(const GEdge *ge, double u)
{
  double val = 0;
  std::list<GFace *> faces = ge->faces();
  std::list<GFace *>::iterator it = faces.begin();
  while(it != faces.end()){
    if ((*it)->geomType() != GEntity::CompoundSurface &&
        (*it)->geomType() != GEntity::DiscreteSurface){
      SPoint2 par = ge->reparamOnFace((*it), u, 1);
      double cc = (*it)->curvature(par);
      val = std::max(cc, val);
    }
    ++it;
  }
  return val;
}


// static double max_surf_curvature_vertex(const GVertex *gv)
// {
//   double val = 0;
//   std::list<GEdge*> l_edges = gv->edges();
//   for (std::list<GEdge*>::const_iterator ite = l_edges.begin();
//        ite != l_edges.end(); ++ite){
//     GEdge *_myGEdge = *ite;
//     Range<double> bounds = _myGEdge->parBounds(0);
//     if (gv == _myGEdge->getBeginVertex())
//       val = std::max(val, max_surf_curvature(_myGEdge, bounds.low()));
//     else
//       val = std::max(val, max_surf_curvature(_myGEdge, bounds.high()));
//   }
//   return val;
// }


SMetric3 metric_based_on_surface_curvature(const GFace *gf, double u, double v,
					   bool surface_isotropic,
					   double d_normal ,
					   double d_tangent_max)
{
  if (gf->geomType() == GEntity::Plane)return SMetric3(1.e-12);
  double cmax, cmin;
  SVector3 dirMax,dirMin;
  cmax = gf->curvatures(SPoint2(u, v),&dirMax, &dirMin, &cmax,&cmin);
  if (cmin == 0)cmin =1.e-12;
  if (cmax == 0)cmax =1.e-12;
  double lambda1 =  ((2 * M_PI) /( fabs(cmin) *  CTX::instance()->mesh.minCircPoints ) );
  double lambda2 =  ((2 * M_PI) /( fabs(cmax) *  CTX::instance()->mesh.minCircPoints ) );
  SVector3 Z = crossprod(dirMax,dirMin);
  if (surface_isotropic)  lambda2 = lambda1 = std::min(lambda2,lambda1);
  dirMin.normalize();
  dirMax.normalize();
  Z.normalize();
  lambda1 = std::max(lambda1, CTX::instance()->mesh.lcMin);
  lambda2 = std::max(lambda2, CTX::instance()->mesh.lcMin);
  lambda1 = std::min(lambda1, CTX::instance()->mesh.lcMax);
  lambda2 = std::min(lambda2, CTX::instance()->mesh.lcMax);
  double lambda3 = std::min(d_normal, CTX::instance()->mesh.lcMax);
  lambda3 = std::max(lambda3, CTX::instance()->mesh.lcMin);
  lambda1 = std::min(lambda1, d_tangent_max);
  lambda2 = std::min(lambda2, d_tangent_max);

  SMetric3 curvMetric (1./(lambda1*lambda1),1./(lambda2*lambda2),
		       1./(lambda3*lambda3),
                       dirMin, dirMax, Z );
  return curvMetric;
}

static SMetric3 metric_based_on_surface_curvature(const GEdge *ge, double u, bool iso_surf)
{
  const GEdgeCompound* ptrCompoundEdge = dynamic_cast<const GEdgeCompound*>(ge);
  if (ptrCompoundEdge){
    double cmax, cmin;
    SVector3 dirMax,dirMin;
    cmax = ptrCompoundEdge->curvatures(u,&dirMax, &dirMin, &cmax,&cmin);
    if (cmin == 0)cmin =1.e-12;
    if (cmax == 0)cmax =1.e-12;
    double lambda2 =  ((2 * M_PI) /( fabs(cmax) *  CTX::instance()->mesh.minCircPoints ) );
    double lambda1 =  ((2 * M_PI) /( fabs(cmin) *  CTX::instance()->mesh.minCircPoints ) );
    SVector3 Z = crossprod(dirMax,dirMin);

    lambda1 = std::max(lambda1, CTX::instance()->mesh.lcMin);
    lambda2 = std::max(lambda2, CTX::instance()->mesh.lcMin);
    lambda1 = std::min(lambda1, CTX::instance()->mesh.lcMax);
    lambda2 = std::min(lambda2, CTX::instance()->mesh.lcMax);

    SMetric3 curvMetric (1. / (lambda1 * lambda1), 1. / (lambda2 * lambda2),
                         1.e-12, dirMin, dirMax, Z);
    return curvMetric;
  }
  else{
    SMetric3 mesh_size(1.e-12);
    std::list<GFace *> faces = ge->faces();
    std::list<GFace *>::iterator it = faces.begin();
    // we choose the metric eigenvectors to be the ones
    // related to the edge ...
    SMetric3 curvMetric = max_edge_curvature_metric(ge, u);
    while(it != faces.end()){
      if (((*it)->geomType() != GEntity::CompoundSurface) &&
          ((*it)->geomType() != GEntity::DiscreteSurface)){
        SPoint2 par = ge->reparamOnFace((*it), u, 1);
        SMetric3 m = metric_based_on_surface_curvature (*it, par.x(), par.y(), iso_surf);
        curvMetric = intersection_conserveM1(curvMetric,m);
      }
      ++it;
    }

    return curvMetric;
  }
}

static SMetric3 metric_based_on_surface_curvature(const GVertex *gv, bool iso_surf)
{
  SMetric3 mesh_size(1.e-15);
  std::list<GEdge*> l_edges = gv->edges();
  for (std::list<GEdge*>::const_iterator ite = l_edges.begin();
       ite != l_edges.end(); ++ite){
    GEdge *_myGEdge = *ite;
    Range<double> bounds = _myGEdge->parBounds(0);

    // ES: Added extra if condition to use the code below only with compund curves
    // This is because we want to call the function
    // metric_based_on_surface_curvature(const GEdge *ge, double u) for the case when
    // ge is a compound edge
    if (_myGEdge->geomType() == GEntity::CompoundCurve){
      if (gv == _myGEdge->getBeginVertex())
        mesh_size = intersection
          (mesh_size,
           metric_based_on_surface_curvature(_myGEdge, bounds.low(), iso_surf));
      else
        mesh_size = intersection
          (mesh_size,
           metric_based_on_surface_curvature(_myGEdge, bounds.high(), iso_surf));
    }
  }
  return mesh_size;
}

// the mesh vertex is classified on a model vertex.  we compute the
// maximum of the curvature of model faces surrounding this point if
// it is classified on a model edge, we do the same for all model
// faces surrounding it if it is on a model face, we compute the
// curvature at this location

static double LC_MVertex_CURV(GEntity *ge, double U, double V)
{
  double Crv = 0;
  switch(ge->dim()){
  case 0:
    Crv = max_edge_curvature((const GVertex *)ge);
    //Crv = std::max(max_surf_curvature_vertex((const GVertex *)ge), Crv);
    // Crv = max_surf_curvature((const GVertex *)ge);
    break;
  case 1:
    {
      GEdge *ged = (GEdge *)ge;
      Crv = ged->curvature(U);
      Crv = std::max(Crv, max_surf_curvature(ged, U));
      // Crv = max_surf_curvature(ged, U);
    }
    break;
  case 2:
    {
      GFace *gf = (GFace *)ge;
      Crv = gf->curvature(SPoint2(U, V));
    }
    break;
  }
  double lc = Crv > 0 ? 2 * M_PI / Crv / CTX::instance()->mesh.minCircPoints : MAX_LC;
  return lc;
}

SMetric3 LC_MVertex_CURV_ANISO(GEntity *ge, double U, double V)
{
  bool iso_surf = CTX::instance()->mesh.lcFromCurvature == 2;

  switch(ge->dim()){
  case 0: return metric_based_on_surface_curvature((const GVertex *)ge, iso_surf);
  case 1: return metric_based_on_surface_curvature((const GEdge *)ge, U, iso_surf);
  case 2: return metric_based_on_surface_curvature((const GFace *)ge, U, V, iso_surf);
  }
  Msg::Error("Curvature control impossible to compute for a volume!");
  return SMetric3();
}

// compute the mesh size at a given vertex due to prescribed sizes at
// mesh vertices
static double LC_MVertex_PNTS(GEntity *ge, double U, double V)
{
  switch(ge->dim()){
  case 0:
    {
      GVertex *gv = (GVertex *)ge;
      double lc = gv->prescribedMeshSizeAtVertex();
      // FIXME we might want to remove this to make all lc treatment consistent
      if(lc >= MAX_LC) return CTX::instance()->lc / 10.;
      return lc;
    }
  case 1:
    {
      GEdge *ged = (GEdge *)ge;
      GVertex *v1 = ged->getBeginVertex();
      GVertex *v2 = ged->getEndVertex();
      if (v1 && v2){
        Range<double> range = ged->parBounds(0);
        double a = (U - range.low()) / (range.high() - range.low());
        double lc = (1 - a) * v1->prescribedMeshSizeAtVertex() +
          (a) * v2->prescribedMeshSizeAtVertex() ;
        // FIXME we might want to remove this to make all lc treatment consistent
        if(lc >= MAX_LC) return CTX::instance()->lc / 10.;
        return lc;
      }
      else
        return MAX_LC;
    }
  default:
    return MAX_LC;
  }
}

// This is the only function that is used by the meshers
double BGM_MeshSize(GEntity *ge, double U, double V,
                    double X, double Y, double Z)
{
  // default lc (mesh size == size of the model)
  double l1 = CTX::instance()->lc;

  // lc from points
  double l2 = MAX_LC;
  if(CTX::instance()->mesh.lcFromPoints && ge->dim() < 2)
    l2 = LC_MVertex_PNTS(ge, U, V);

  // lc from curvature
  double l3 = MAX_LC;
  if(CTX::instance()->mesh.lcFromCurvature && ge->dim() < 3)
    l3 = LC_MVertex_CURV(ge, U, V);

  // lc from fields
  double l4 = MAX_LC;
  FieldManager *fields = ge->model()->getFields();
  if(fields->getBackgroundField() > 0){
    Field *f = fields->get(fields->getBackgroundField());
    if(f) l4 = (*f)(X, Y, Z, ge);
  }

  // take the minimum, then constrain by lcMin and lcMax
  double lc = std::min(std::min(std::min(l1, l2), l3), l4);
  lc = std::max(lc, CTX::instance()->mesh.lcMin);
  lc = std::min(lc, CTX::instance()->mesh.lcMax);

  if(lc <= 0.){
    Msg::Error("Wrong mesh element size lc = %g (lcmin = %g, lcmax = %g)",
               lc, CTX::instance()->mesh.lcMin, CTX::instance()->mesh.lcMax);
    lc = l1;
  }

  //Msg::Debug("BGM X,Y,Z=%g,%g,%g L4=%g L3=%g L2=%g L1=%g LC=%g LFINAL=%g DIM =%d ",
  //X, Y, Z, l4, l3, l2, l1, lc, lc * CTX::instance()->mesh.lcFactor, ge->dim());

  //Emi fix
  //if (lc == l1) lc /= 10.;

  return lc * CTX::instance()->mesh.lcFactor;
}


// anisotropic version of the background field
SMetric3 BGM_MeshMetric(GEntity *ge,
                        double U, double V,
                        double X, double Y, double Z)
{

  // Metrics based on element size
  // Element size  = min. between default lc and lc from point (if applicable), constrained by lcMin and lcMax
  double lc = CTX::instance()->lc;
  if(CTX::instance()->mesh.lcFromPoints && ge->dim() < 2) lc = std::min(lc, LC_MVertex_PNTS(ge, U, V));
  lc = std::max(lc, CTX::instance()->mesh.lcMin);
  lc = std::min(lc, CTX::instance()->mesh.lcMax);
  if(lc <= 0.){
    Msg::Error("Wrong mesh element size lc = %g (lcmin = %g, lcmax = %g)",
               lc, CTX::instance()->mesh.lcMin, CTX::instance()->mesh.lcMax);
    lc = CTX::instance()->lc;
  }
  SMetric3 m0(1./(lc*lc));

  // Intersect with metrics from fields if applicable
  FieldManager *fields = ge->model()->getFields();
  SMetric3 m1 = m0;
  if(fields->getBackgroundField() > 0){
    Field *f = fields->get(fields->getBackgroundField());
    if(f) {
      SMetric3 l4;
      if (!f->isotropic()) (*f)(X, Y, Z, l4, ge);
      else {
        const double L = (*f)(X, Y, Z, ge);
        l4 = SMetric3(1/(L*L));
      }
      m1 = intersection(l4, m0);
    }
  }

  // Intersect with metrics from curvature if applicable
  SMetric3 m = (CTX::instance()->mesh.lcFromCurvature && ge->dim() < 3) ?
      intersection(m1, LC_MVertex_CURV_ANISO(ge, U, V)) : m1;

  return m;

}

bool Extend1dMeshIn2dSurfaces()
{
  return CTX::instance()->mesh.lcExtendFromBoundary ? true : false;
}

bool Extend2dMeshIn3dVolumes()
{
  return CTX::instance()->mesh.lcExtendFromBoundary ? true : false;
}

void backgroundMesh::set(GFace *gf)
{
  if (_current) delete _current;
  _current = new backgroundMesh(gf);
}

void backgroundMesh::setCrossFieldsByDistance(GFace *gf)
{
  if (_current) delete _current;
  _current = new backgroundMesh(gf, true);
}

void backgroundMesh::unset()
{
  if (_current) delete _current;
  _current = 0;
}

backgroundMesh::backgroundMesh(GFace *_gf, bool cfd)
#if defined(HAVE_ANN)
  : _octree(0), uv_kdtree(0), nodes(0), angle_nodes(0), angle_kdtree(0)
#endif
{

  if (cfd){
    Msg::Info("Building A Cross Field Using Closest Distance");
    propagateCrossFieldByDistance(_gf);
    return;
  }

  // create a bunch of triangles on the parametric space
  // those triangles are local to the backgroundMesh so that
  // they do not depend on the actual mesh that can be deleted

  std::set<SPoint2> myBCNodes;
  for (unsigned int i = 0; i < _gf->triangles.size(); i++){
    MTriangle *e = _gf->triangles[i];
    MVertex *news[3];
    for (int j=0;j<3;j++){
      MVertex *v = e->getVertex(j);
      std::map<MVertex*,MVertex*>::iterator it = _3Dto2D.find(v);
      MVertex *newv =0;
      if (it == _3Dto2D.end()){
        SPoint2 p;
        reparamMeshVertexOnFace(v, _gf, p);
        newv = new MVertex (p.x(), p.y(), 0.0);
        _vertices.push_back(newv);
        _3Dto2D[v] = newv;
        _2Dto3D[newv] = v;
	if(v->onWhat()->dim()<2) myBCNodes.insert(p);
      }
      else newv = it->second;
      news[j] = newv;
    }
    _triangles.push_back(new MTriangle(news[0],news[1],news[2]));
  }

#if defined(HAVE_ANN)
  //printf("creating uv kdtree %d \n", myBCNodes.size());
    index = new ANNidx[2];
    dist  = new ANNdist[2];
    nodes = annAllocPts(myBCNodes.size(), 3);
    std::set<SPoint2>::iterator itp = myBCNodes.begin();
    int ind = 0;
    while (itp != myBCNodes.end()){
      SPoint2 pt = *itp;
      //fprintf(of, "SP(%g,%g,%g){%g};\n", pt.x(), pt.y(), 0.0, 10000);
      nodes[ind][0] = pt.x();
      nodes[ind][1] = pt.y();
      nodes[ind][2] = 0.0;
      itp++; ind++;
    }
    uv_kdtree = new ANNkd_tree(nodes, myBCNodes.size(), 3);
#endif

  // build a search structure
  _octree = new MElementOctree(_triangles);

  // compute the mesh sizes at nodes
  if (CTX::instance()->mesh.lcFromPoints){
    propagate1dMesh(_gf);
  }
  else {
    std::map<MVertex*, MVertex*>::iterator itv2 = _2Dto3D.begin();
    for ( ; itv2 != _2Dto3D.end(); ++itv2){
      _sizes[itv2->first] = CTX::instance()->mesh.lcMax;
    }
  }
  // ensure that other criteria are fullfilled
  updateSizes(_gf);

  // compute optimal mesh orientations
  propagatecrossField(_gf);

  _3Dto2D.clear();
  _2Dto3D.clear();
}

backgroundMesh::~backgroundMesh()
{
  for (unsigned int i = 0; i < _vertices.size(); i++) delete _vertices[i];
  for (unsigned int i = 0; i < _triangles.size(); i++) delete _triangles[i];
  if (_octree)delete _octree;
#if defined(HAVE_ANN)
  if(uv_kdtree) delete uv_kdtree;
  if(angle_kdtree) delete angle_kdtree;
  if(nodes) annDeallocPts(nodes);
  if(angle_nodes) annDeallocPts(angle_nodes);
  delete[]index;
  delete[]dist;
#endif
}

static void propagateValuesOnFace(GFace *_gf,
                                  std::map<MVertex*,double> &dirichlet,
				  bool in_parametric_plane = false)
{
#if defined(HAVE_SOLVER)
  linearSystem<double> *_lsys = 0;
#if defined(HAVE_PETSC) && !defined(HAVE_TAUCS)
  _lsys = new linearSystemPETSc<double>;
#elif defined(HAVE_GMM) && !defined(HAVE_TAUCS)
  linearSystemGmm<double> *_lsysb = new linearSystemGmm<double>;
  _lsysb->setGmres(1);
  _lsys = _lsysb;
#elif defined(HAVE_TAUCS)
  _lsys = new linearSystemCSRTaucs<double>;
#else
  _lsys = new linearSystemFull<double>;
#endif

  dofManager<double> myAssembler(_lsys);

  // fix boundary conditions
  std::map<MVertex*, double>::iterator itv = dirichlet.begin();
  for ( ; itv != dirichlet.end(); ++itv){
    myAssembler.fixVertex(itv->first, 0, 1, itv->second);
  }


  // Number vertices
  std::set<MVertex*> vs;
  for (unsigned int k = 0; k < _gf->triangles.size(); k++)
    for (int j=0;j<3;j++)vs.insert(_gf->triangles[k]->getVertex(j));
  for (unsigned int k = 0; k < _gf->quadrangles.size(); k++)
    for (int j=0;j<4;j++)vs.insert(_gf->quadrangles[k]->getVertex(j));

  std::map<MVertex*,SPoint3> theMap;
  if ( in_parametric_plane) {
    for (std::set<MVertex*>::iterator it = vs.begin(); it != vs.end(); ++it){
      SPoint2 p;
      reparamMeshVertexOnFace ( *it, _gf, p);
      theMap[*it] = SPoint3((*it)->x(),(*it)->y(),(*it)->z());
      (*it)->setXYZ(p.x(),p.y(),0.0);
    }
  }

  for (std::set<MVertex*>::iterator it = vs.begin(); it != vs.end(); ++it)
    myAssembler.numberVertex(*it, 0, 1);

  // Assemble
  simpleFunction<double> ONE(1.0);
  laplaceTerm l(0, 1, &ONE);
  for (unsigned int k = 0; k < _gf->triangles.size(); k++){
    MTriangle *t = _gf->triangles[k];
    SElement se(t);
    l.addToMatrix(myAssembler, &se);
  }

  // Solve
  if (myAssembler.sizeOfR()){
    _lsys->systemSolve();
  }

  // save solution
  for (std::set<MVertex*>::iterator it = vs.begin(); it != vs.end(); ++it){
    myAssembler.getDofValue(*it, 0, 1, dirichlet[*it]);
  }

  if ( in_parametric_plane) {
    for (std::set<MVertex*>::iterator it = vs.begin(); it != vs.end(); ++it){
      SPoint3 p = theMap[(*it)];
      (*it)->setXYZ(p.x(),p.y(),p.z());
    }
  }
  delete _lsys;
#endif
}

void backgroundMesh::propagate1dMesh(GFace *_gf)
{
  std::list<GEdge*> e;// = _gf->edges();
  replaceMeshCompound(_gf, e);
  std::list<GEdge*>::const_iterator it = e.begin();
  std::map<MVertex*,double> sizes;

  for( ; it != e.end(); ++it ){
    if (!(*it)->isSeam(_gf)){
      for(unsigned int i = 0; i < (*it)->lines.size(); i++ ){
        MVertex *v1 = (*it)->lines[i]->getVertex(0);
        MVertex *v2 = (*it)->lines[i]->getVertex(1);
	if (v1 != v2){
	  double d = sqrt((v1->x() - v2->x()) * (v1->x() - v2->x()) +
			  (v1->y() - v2->y()) * (v1->y() - v2->y()) +
			  (v1->z() - v2->z()) * (v1->z()  -v2->z()));
	  for (int k=0;k<2;k++){
	    MVertex *v = (*it)->lines[i]->getVertex(k);
	    std::map<MVertex*, double>::iterator itv = sizes.find(v);
	    if (itv == sizes.end())
	      sizes[v] = log(d);
	    else
	      itv->second = 0.5 * (itv->second + log(d));
	  }
	}
      }
    }
  }

  propagateValuesOnFace(_gf, sizes);

  std::map<MVertex*,MVertex*>::iterator itv2 = _2Dto3D.begin();
  for ( ; itv2 != _2Dto3D.end(); ++itv2){
    MVertex *v_2D = itv2->first;
    MVertex *v_3D = itv2->second;
    _sizes[v_2D] = exp(sizes[v_3D]);
  }
}

crossField2d::crossField2d(MVertex* v, GEdge* ge)
{
  double p;
  bool success = reparamMeshVertexOnEdge(v, ge, p);
  if (!success){
    Msg::Warning("cannot reparametrize a point in crossField");
    _angle = 0;
    return;
  }
  SVector3 t = ge->firstDer (p);
  t.normalize();
  _angle = atan2 (t.y(),t.x());
  crossField2d::normalizeAngle (_angle);
}

void backgroundMesh::propagateCrossFieldByDistance(GFace *_gf)
{
  std::list<GEdge*> e;
  replaceMeshCompound(_gf, e);

  std::list<GEdge*>::const_iterator it = e.begin();
  std::map<MVertex*,double> _cosines4,_sines4;
  std::map<MVertex*,SPoint2> _param;

  for( ; it != e.end(); ++it ){
    if (!(*it)->isSeam(_gf)){
      for(unsigned int i = 0; i < (*it)->lines.size(); i++ ){
        MVertex *v[2];
        v[0] = (*it)->lines[i]->getVertex(0);
        v[1] = (*it)->lines[i]->getVertex(1);
        SPoint2 p1,p2;
        reparamMeshEdgeOnFace(v[0],v[1],_gf,p1,p2);
	/* a correct way of computing angles  */
	Pair<SVector3, SVector3> der = _gf->firstDer((p1+p2)*.5);
	SVector3 t1 = der.first();
	SVector3 t2 (v[1]->x()-v[0]->x(),v[1]->y()-v[0]->y(),v[1]->z()-v[0]->z());
	t1.normalize();
	t2.normalize();
	double _angle = angle (t1,t2);	
	//        double angle = atan2 ( p1.y()-p2.y() , p1.x()-p2.x() );
        crossField2d::normalizeAngle (_angle);
        for (int i=0;i<2;i++){
          std::map<MVertex*,double>::iterator itc = _cosines4.find(v[i]);
          std::map<MVertex*,double>::iterator its = _sines4.find(v[i]);
          if (itc != _cosines4.end()){
            itc->second  = 0.5*(itc->second + cos(4*_angle));
            its->second  = 0.5*(its->second + sin(4*_angle));
          }
          else {
	    _param[v[i]] = (i==0) ? p1 : p2;
            _cosines4[v[i]] = cos(4*_angle);
            _sines4[v[i]] = sin(4*_angle);
          }
        }
      }
    }
  }

#if defined(HAVE_ANN)
  index = new ANNidx[_NBANN];
  dist  = new ANNdist[_NBANN];
  angle_nodes = annAllocPts(_cosines4.size(), 3);
  std::map<MVertex*,double>::iterator itp = _cosines4.begin();
  int ind = 0;
  _sin.clear();
  _cos.clear();
  while (itp !=  _cosines4.end()){
    MVertex *v = itp->first;
    double c = itp->second;
    SPoint2 pt = _param[v];
    double s = _sines4[v];
    angle_nodes[ind][0] = pt.x();
    angle_nodes[ind][1] = pt.y();
    angle_nodes[ind][2] = 0.0;
    _cos.push_back(c);
    _sin.push_back(s);
    itp++;ind++;
  }
  angle_kdtree = new ANNkd_tree(angle_nodes, _cosines4.size(), 3);
#endif
}

inline double myAngle (const SVector3 &a, const SVector3 &b, const SVector3 &d){
  double cosTheta = dot(a,b);
  double sinTheta = dot(crossprod(a,b),d);
  return atan2 (sinTheta,cosTheta);  
}


void backgroundMesh::propagatecrossField(GFace *_gf)
{

  std::map<MVertex*,double> _cosines4,_sines4;

  std::list<GEdge*> e;
  replaceMeshCompound(_gf, e);

  std::list<GEdge*>::const_iterator it = e.begin();

  for( ; it != e.end(); ++it ){
    if (!(*it)->isSeam(_gf)){
      for(unsigned int i = 0; i < (*it)->lines.size(); i++ ){
        MVertex *v[2];
        v[0] = (*it)->lines[i]->getVertex(0);
        v[1] = (*it)->lines[i]->getVertex(1);
        SPoint2 p1,p2;
        reparamMeshEdgeOnFace(v[0],v[1],_gf,p1,p2);
	Pair<SVector3, SVector3> der = _gf->firstDer((p1+p2)*.5);
	SVector3 t1 = der.first();
	SVector3 s2 = der.second();
	SVector3 n = crossprod(t1,s2);
	n.normalize();
	SVector3 t2 (v[1]->x()-v[0]->x(),v[1]->y()-v[0]->y(),v[1]->z()-v[0]->z());
	t1.normalize();
	t2.normalize();
	double _angle = myAngle (t1,t2,n);	
	//	printf("GFACE %d %g %g %g %g\n",_gf->tag(),t1.x(),t1.y(),t1.z(),_angle*180/M_PI);
	//	printf("angle = %12.5E\n",_angle);
	//	angle_ = atan2 ( p1.y()-p2.y() , p1.x()-p2.x() );
        //double angle = atan2 ( v[0]->y()-v[1]->y() , v[0]->x()- v[1]->x() );
        //double angle = atan2 ( v0->y()-v1->y() , v0->x()- v1->x() );
        crossField2d::normalizeAngle (_angle);
	//	SVector3 s2 = der.second();
	//	s2.normalize();
	SVector3 x = t1 * cos (_angle) + crossprod(n,t1) * sin (_angle);
	//	printf("angle = %g --> %g %g %g vs %g %g %g\n",_angle,x.x(),x.y(),x.z(),t2.x(),t2.y(),t2.z());
	//	printf("GFACE %d GEDGE %d %g %g %g %g\n",_gf->tag(),(*it)->tag(),t1.x(),t1.y(),t1.z(),_angle*180/M_PI);
        for (int i=0;i<2;i++){
          std::map<MVertex*,double>::iterator itc = _cosines4.find(v[i]);
          std::map<MVertex*,double>::iterator its = _sines4.find(v[i]);
          if (itc != _cosines4.end()){
            itc->second  = 0.5*(itc->second + cos(4*_angle));
            its->second  = 0.5*(its->second + sin(4*_angle));
          }
          else {
            _cosines4[v[i]] = cos(4*_angle);
            _sines4[v[i]] = sin(4*_angle);
          }
        }
      }
    }
  }

  propagateValuesOnFace(_gf,_cosines4,false);
  propagateValuesOnFace(_gf,_sines4,false);

  std::map<MVertex*,MVertex*>::iterator itv2 = _2Dto3D.begin();
  for ( ; itv2 != _2Dto3D.end(); ++itv2){
    MVertex *v_2D = itv2->first;
    MVertex *v_3D = itv2->second;
    double angle = atan2(_sines4[v_3D],_cosines4[v_3D]) / 4.0;
    crossField2d::normalizeAngle (angle);
    _angles[v_2D] = angle;
  }
}

void backgroundMesh::updateSizes(GFace *_gf)
{
  std::map<MVertex*,double>::iterator itv = _sizes.begin();
  for ( ; itv != _sizes.end(); ++itv){
    SPoint2 p;
    MVertex *v = _2Dto3D[itv->first];
    double lc;
    if (v->onWhat()->dim() == 0){
      lc = BGM_MeshSize(v->onWhat(), 0,0,v->x(),v->y(),v->z());
    }
    else if (v->onWhat()->dim() == 1){
      double u;
      v->getParameter(0, u);
      lc = BGM_MeshSize(v->onWhat(), u, 0, v->x(), v->y(), v->z());
    }
    else{
      reparamMeshVertexOnFace(v, _gf, p);
      lc = BGM_MeshSize(_gf, p.x(), p.y(), v->x(), v->y(), v->z());
    }
    // printf("2D -- %g %g 3D -- %g %g\n",p.x(),p.y(),v->x(),v->y());
    itv->second = std::min(lc,itv->second);
    itv->second = std::max(itv->second, CTX::instance()->mesh.lcMin);
    itv->second = std::min(itv->second, CTX::instance()->mesh.lcMax);
  }
  // do not allow large variations in the size field
  // (Int. J. Numer. Meth. Engng. 43, 1143-1165 (1998) MESH GRADATION
  // CONTROL, BOROUCHAKI, HECHT, FREY)
  std::set<MEdge,Less_Edge> edges;
  for (unsigned int i = 0; i < _triangles.size(); i++){
    for (int j = 0; j < _triangles[i]->getNumEdges(); j++){
      edges.insert(_triangles[i]->getEdge(j));
    }
  }
  const double _beta = 1.3;
  for (int i=0;i<0;i++){
    std::set<MEdge,Less_Edge>::iterator it = edges.begin();
    for ( ; it != edges.end(); ++it){
      MVertex *v0 = it->getVertex(0);
      MVertex *v1 = it->getVertex(1);
      MVertex *V0 = _2Dto3D[v0];
      MVertex *V1 = _2Dto3D[v1];
      std::map<MVertex*,double>::iterator s0 = _sizes.find(V0);
      std::map<MVertex*,double>::iterator s1 = _sizes.find(V1);
      if (s0->second < s1->second)s1->second = std::min(s1->second,_beta*s0->second);
      else s0->second = std::min(s0->second,_beta*s1->second);
    }
  }
}

bool backgroundMesh::inDomain (double u, double v, double w) const
{
  return _octree->find(u, v, w, 2, true) != 0;
}

double backgroundMesh::operator() (double u, double v, double w) const
{
  double uv[3] = {u, v, w};
  double uv2[3];
  MElement *e = _octree->find(u, v, w, 2, true);
  if (!e) {
#if defined(HAVE_ANN)
    //printf("BGM octree not found --> find in kdtree \n");
    double pt[3] = {u, v, 0.0};
    uv_kdtree->annkSearch(pt, 2, index, dist);
    SPoint3  p1(nodes[index[0]][0], nodes[index[0]][1], nodes[index[0]][2]);
    SPoint3  p2(nodes[index[1]][0], nodes[index[1]][1], nodes[index[1]][2]);
    SPoint3 pnew; double d;
    signedDistancePointLine(p1, p2, SPoint3(u, v, 0.), d, pnew);
    e = _octree->find(pnew.x(), pnew.y(), 0.0, 2, true);
#endif
    if(!e){
      Msg::Error("BGM octree: cannot find UVW=%g %g %g", u, v, w);
      return -1000.0;//0.4;
    }
  }
  e->xyz2uvw(uv, uv2);
  std::map<MVertex*,double>::const_iterator itv1 = _sizes.find(e->getVertex(0));
  std::map<MVertex*,double>::const_iterator itv2 = _sizes.find(e->getVertex(1));
  std::map<MVertex*,double>::const_iterator itv3 = _sizes.find(e->getVertex(2));
  return itv1->second * (1-uv2[0]-uv2[1]) + itv2->second * uv2[0] + itv3->second * uv2[1];
}

double backgroundMesh::getAngle(double u, double v, double w) const
{
  // JFR :
  // we can use closest point for computing
  // cross field angles : this allow NOT to
  // generate a spurious mesh and solve a PDE
  if (!_octree){
#if defined(HAVE_ANN)
    double pt[3] = {u,v,0.0};
    angle_kdtree->annkSearch(pt, _NBANN, index, dist);
    double SINE = 0.0 , COSINE = 0.0;
    for (int i=0;i<_NBANN;i++){
      SINE += _sin[index[i]];
      COSINE += _cos[index[i]];
      //      printf("%2d %2d %12.5E %12.5E\n",i,index[i],_sin[index[i]],_cos[index[i]]);
    }
    double angle = atan2(SINE,COSINE)/4.0;
    crossField2d::normalizeAngle (angle);
    return angle;
#endif
  }

  // HACK FOR LEWIS
  // h = 1+30(y-x^2)^2  + (1-x)^2
  //  double x = u;
  //  double y = v;
  //  double dhdx = 30 * 2 * (y-x*x) * (-2*x) - 2 * (1-x);
  //  double dhdy = 30 * 2 * (y-x*x);
  //  double angles = atan2(y,x*x);
  //  crossField2d::normalizeAngle (angles);
  //  return angles;

  double uv[3] = {u, v, w};
  double uv2[3];
  MElement *e = _octree->find(u, v, w, 2, true);
  if (!e) {
#if defined(HAVE_ANN)
    //printf("BGM octree not found --> find in kdtree \n");
    double pt[3] = {u,v,0.0};
    uv_kdtree->annkSearch(pt, 2, index, dist);
    SPoint3  p1(nodes[index[0]][0], nodes[index[0]][1], nodes[index[0]][2]);
    SPoint3  p2(nodes[index[1]][0], nodes[index[1]][1], nodes[index[1]][2]);
    SPoint3 pnew; double d;
    signedDistancePointLine(p1, p2, SPoint3(u, v, 0.), d, pnew);
    e = _octree->find(pnew.x(), pnew.y(), 0., 2, true);
#endif
    if(!e){
      Msg::Error("BGM octree angle: cannot find UVW=%g %g %g", u, v, w);
      return -1000.0;
    }
  }
  e->xyz2uvw(uv, uv2);
  std::map<MVertex*,double>::const_iterator itv1 = _angles.find(e->getVertex(0));
  std::map<MVertex*,double>::const_iterator itv2 = _angles.find(e->getVertex(1));
  std::map<MVertex*,double>::const_iterator itv3 = _angles.find(e->getVertex(2));

  double cos4 = cos (4*itv1->second) * (1-uv2[0]-uv2[1]) +
    cos (4*itv2->second) * uv2[0] +
    cos (4*itv3->second) * uv2[1] ;
  double sin4 = sin (4*itv1->second) * (1-uv2[0]-uv2[1]) +
    sin (4*itv2->second) * uv2[0] +
    sin (4*itv3->second) * uv2[1] ;
  double angle = atan2(sin4,cos4)/4.0;
  crossField2d::normalizeAngle (angle);

  return angle;
}

void backgroundMesh::print(const std::string &filename, GFace *gf,
                           const std::map<MVertex*,double> &_whatToPrint) const
{
  FILE *f = fopen (filename.c_str(),"w");
  fprintf(f,"View \"Background Mesh\"{\n");
  for(unsigned int i=0;i<_triangles.size();i++){
    MVertex *v1 = _triangles[i]->getVertex(0);
    MVertex *v2 = _triangles[i]->getVertex(1);
    MVertex *v3 = _triangles[i]->getVertex(2);
    std::map<MVertex*,double>::const_iterator itv1 = _whatToPrint.find(v1);
    std::map<MVertex*,double>::const_iterator itv2 = _whatToPrint.find(v2);
    std::map<MVertex*,double>::const_iterator itv3 = _whatToPrint.find(v3);
    if (!gf){
      fprintf(f,"ST(%g,%g,%g,%g,%g,%g,%g,%g,%g) {%g,%g,%g};\n",
              v1->x(),v1->y(),v1->z(),
              v2->x(),v2->y(),v2->z(),
              v3->x(),v3->y(),v3->z(),itv1->second,itv2->second,itv3->second);
      /*
      fprintf(f,"ST(%g,%g,%g,%g,%g,%g,%g,%g,%g) {%g,%g,%g};\n",
              v1->x(),v1->y(),v1->z(),
              v2->x(),v2->y(),v2->z(),
              v3->x(),v3->y(),v3->z(),
	      getAngle(v1->x(),v1->y(),v1->z()),
	      getAngle(v2->x(),v2->y(),v2->z()),
	      getAngle(v3->x(),v3->y(),v3->z()));
      */
    }
    else {
      
      GPoint p1 = gf->point(SPoint2(v1->x(),v1->y()));
      GPoint p2 = gf->point(SPoint2(v2->x(),v2->y()));
      GPoint p3 = gf->point(SPoint2(v3->x(),v3->y()));
      fprintf(f,"ST(%g,%g,%g,%g,%g,%g,%g,%g,%g) {%g,%g,%g};\n",
              p1.x(),p1.y(),p1.z(),
              p2.x(),p2.y(),p2.z(),
              p3.x(),p3.y(),p3.z(),itv1->second,itv2->second,itv3->second);
      /*
      fprintf(f,"ST(%g,%g,%g,%g,%g,%g,%g,%g,%g) {%g,%g,%g};\n",
              v1->x(),v1->y(),v1->z(),
              v2->x(),v2->y(),v2->z(),
              v3->x(),v3->y(),v3->z(),
              itv1->second,itv2->second,itv3->second);
      */
    }
  }
  fprintf(f,"};\n");
  fclose(f);
}

MElementOctree* backgroundMesh::get_octree(){
  return _octree;
}

backgroundMesh* backgroundMesh::_current = 0;
