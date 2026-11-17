#include "Engine.h"
#include "Osc.h"
#include <QApplication>
#include <map>
#include <cstdio>
int main(int argc,char**argv){ QApplication a(argc,argv); Engine e; QString err,w; e.initialize(&err); e.loadProject(argv[1],nullptr,&w); OscNamespace ns(&e); ns.refresh(); std::map<QString,int> cnt; for(const QString&p: ns.paths()){ const auto*n=ns.node(p); if(!n||!(n->access&1)) continue; QStringList s=p.split('/',Qt::SkipEmptyParts); QString key="/"+s.value(0)+ (s.size()>2? "/*/"+s.value(2):""); cnt[key]++; } for(auto&[k,v]:cnt) std::printf("%6d %s\n",v,qPrintable(k)); return 0; }
