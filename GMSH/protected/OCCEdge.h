// Gmsh - Copyright (C) 1997-2013 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@geuz.org>.

#ifndef _OCC_EDGE_H_
#define _OCC_EDGE_H_

#include "GmshConfig.h"
#include "GEdge.h"
#include "GModel.h"
#include "OCCVertex.h"
#include "Range.h"

class OCCFace;

#if defined(HAVE_OCC)

class OCCEdge : public GEdge {
 protected:
  TopoDS_Edge _replacement;
  TopoDS_Edge c;
  TopoDS_Edge c_rev;
  Standard_Real s0, s1;
  Handle(Geom_Curve) curve;
  mutable Handle(Geom2d_Curve) curve2d;
  mutable GFace *trimmed;
 public:
  OCCEdge(GModel *model, TopoDS_Edge _e, int num, GVertex *v1, GVertex *v2);
  virtual ~OCCEdge();
  virtual Range<double> parBounds(int i) const;
  virtual GeomType geomType() const;
  virtual bool degenerate(int) const { return BRep_Tool::Degenerated(c); }
  virtual GPoint point(double p) const;
  virtual SVector3 firstDer(double par) const;
  virtual double curvature (double par) const;
  virtual SPoint2 reparamOnFace(const GFace *face, double epar, int dir) const;
  virtual GPoint closestPoint(const SPoint3 &queryPoint, double &param) const;
  ModelType getNativeType() const { return OpenCascadeModel; }
  void * getNativePtr() const { return (void*)&c; }
  virtual int minimumMeshSegments () const;
  virtual int minimumDrawSegments () const;
  bool is3D() const { return !curve.IsNull(); }
  void setTrimmed(OCCFace *);
  bool isSeam(const GFace *) const;
  virtual void writeGEO(FILE *fp);
  TopoDS_Edge getTopoDS_Edge() const {return c;}
  TopoDS_Edge getTopoDS_EdgeOld() const {return _replacement;}
  void replaceEndingPointsInternals(GVertex *, GVertex *);
};

#endif

#endif
