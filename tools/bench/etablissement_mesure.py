"""Etablissement de la mesure des courants (porte 4, revue du 30/10/2026 soir), a partir des anneaux figes E20B/E20C/E20D
des captures du pilote de seance. Pour chaque periode : rapports cycliques recalcules (svpwm d'EBiCS, depuis u_d, u_q et
l'angle de E20C), declenchement selon la logique du build, marge de conduction du bras bas avant la fin de l'echantillon
(memes conventions que inc/dyn_adc_robuste.h), et erreur de la voie brute (E20D) par rapport a un ajustement sinusoidal de
l'anneau (sur les echantillons a marge >= 700 ticks). Resultat : erreur par tranche de marge, declenchement par defaut et
deplace separes. C'est ce releve qui fixe DYN_STAB_DEFAUT et DYN_STAB_DYN (inc/config.h).
Usage : python -I tools/bench/etablissement_mesure.py [dossier des captures] [capture.csv=ebics|r1|r2 ...]"""
import csv, ast, math, sys
T=2000; SQ3=28
def svpwm(a,b):
    Ua=(SQ3*T*a)>>4; Ub=-T*b; X=Ub; Y=(Ua+Ub)>>1; Z=(Ub-Ua)>>1; s=[0,0,0]
    if (Y>=0 and Z<0 and X>0) or (Y<0 and Z>=0 and X<=0):
        s[0]=((T+X-Z)>>12)+(T>>1); s[1]=s[0]+(Z>>11); s[2]=s[1]-(X>>11)
    if (Y>=0 and Z>=0) or (Y<0 and Z<0):
        s[0]=((T+Y-Z)>>12)+(T>>1); s[1]=s[0]+(Z>>11); s[2]=s[0]-(Y>>11)
    if (Y<0 and Z>=0 and X>0) or (Y>=0 and Z<0 and X<=0):
        s[0]=((T+Y-X)>>12)+(T>>1); s[2]=s[0]-(Y>>11); s[1]=s[2]+(X>>11)
    return s
def sw(ud,uq,deg):
    th=math.radians(deg); c,sn=math.cos(th),math.sin(th)
    return svpwm(int(round(ud*c+uq*sn)), int(round(-ud*sn+uq*c)))
def anneaux(f):
    rows=list(csv.reader(open(f,encoding='utf-8',errors='replace')))
    R={}
    for r in rows[1:]:
        if r[1] not in ('E20B','E20C','E20D'): continue
        d=ast.literal_eval(r[3])
        if d.get('figee')!=1 or d.get('pleine')!=1 or d.get('rang',255)==255: continue
        R.setdefault(d['pt_seq'],{}).setdefault(r[1],{})[d['rang']]=d
    return R

D=sys.argv[1].rstrip('/')+'/' if len(sys.argv)>1 else '../aoci-firmware-re/validation/porte-4/'
T=2000; TH=1810; AV=65; DEF=1990; TE=45; DTG=25
def imax(s): return max(range(3), key=lambda p:(s[p],-p))
def robuste(sp,sn,th=TH):
    mp,mn=imax(sp),imax(sn); dyn=sp[mp]>th or sn[mn]>th; m=mp if dyn else mn
    if dyn and mp!=mn: return DEF
    return (min(sp[m],sn[m])-AV) if dyn else DEF
def ebics(s):
    m=imax(s); return s[m]-AV if s[m]>TH else DEF
def marge(s,p,cc4):
    tech=cc4+TE
    if tech>2*T-s[p]: return -1
    return tech-(s[p]+DTG)
# capture -> logique de placement du build : ebics (dyn_adc_state, CC4 immediat), r1 (8295942 : robuste, CC4 immediat),
# r2 (b6021d8 : robuste, CC4 precharge, decision cumulee), r3 (comme r2, seuil dynamique 1560) ; ajouter ici les captures suivantes
FICH={'perf-niveau5-12h21.csv':'ebics','perf-niveau5-12h21-2.csv':'ebics','perf-niveau5-12h21-3.csv':'r1','perf-niveau5-15h19.csv':'r2'}
if len(sys.argv)>2: FICH={k:v for k,v in (x.split('=') for x in sys.argv[2:])}
bins={}; detail=[]
for fn,mode in FICH.items():
    R=anneaux(D+fn)
    for seq,g in R.items():
        c=g.get('E20C',{}); a=g.get('E20D',{})
        if len(c)<128 or len(a)<128: continue
        S={k:sw(c[k]['ud']*1.0,c[k]['uq']*1.0,c[k]['angle_deg']) for k in c if c[k].get('foc')}
        rows=[k for k in sorted(c) if all(j in S for j in (k-1,k-2,k-3)) and k in a]
        if len(rows)<60: continue
        M={}
        for k in rows:
            if mode=='ebics': combos=[(S[k-1],ebics(S[k-2])),(S[k-2],ebics(S[k-2]))]
            elif mode=='r1': cc=robuste(S[k-2],S[k-1]); combos=[(S[k-1],cc),(S[k-2],cc)]
            else:
                th=1560 if mode=='r3' else TH   # r3 : revue du 30/10 soir (DYN_SEUIL_ROUTE)
                c1=robuste(S[k-2],S[k-1],th); c2=robuste(S[k-3],S[k-2],th)
                combos=[(S[k-2],c2),(S[k-2],c1),(S[k-1],c1)]
            M[k]=[(min(marge(s,p,cc) for s,cc in combos),max(marge(s,p,cc) for s,cc in combos),'dyn' if all(cc!=DEF for s,cc in combos) else ('def' if all(cc==DEF for s,cc in combos) else 'mix'),min((2*T-s[p])-(cc+TE) for s,cc in combos)) for p in range(3)]
        best=None
        for sgn in (1,-1):
            A=[];B=[]
            for k in rows:
                th=math.radians(c[k]['angle_deg'])
                for p in range(3):
                    if M[k][p][0]<700 or abs(a[k]['av'][p])>=127: continue
                    ang=th-sgn*p*2*math.pi/3; A.append((math.cos(ang),-math.sin(ang))); B.append(a[k]['av'][p])
            if len(A)<40: continue
            sxx=sum(x*x for x,y in A); syy=sum(y*y for x,y in A); sxy=sum(x*y for x,y in A)
            bx=sum(x*b for (x,y),b in zip(A,B)); by=sum(y*b for (x,y),b in zip(A,B)); det=sxx*syy-sxy*sxy
            re=(bx*syy-by*sxy)/det; im=(by*sxx-bx*sxy)/det
            res=(sum((b-(x*re+y*im))**2 for (x,y),b in zip(A,B))/len(A))**.5
            if best is None or res<best[0]: best=(res,sgn,re,im,len(A))
        res,sgn,re,im,n=best
        print(fn,seq,'lignes',len(rows),'I=%.0f rms=%.1f n=%d'%(abs(complex(re,im)),res,n))
        if abs(complex(re,im))<40: continue
        for k in rows:
            th=math.radians(c[k]['angle_deg'])
            for p in range(3):
                pred=re*math.cos(th-sgn*p*2*math.pi/3)-im*math.sin(th-sgn*p*2*math.pi/3)
                v=a[k]['av'][p]; e=abs(v-pred) if abs(v)<127 else 999
                lo,hi,ty,ro=M[k][p]
                if ty=='mix': continue
                if lo<0 and hi<0: key=-100
                elif lo<0: continue      # incertain : bras bas peut-etre ouvert
                else: key=lo//50*50
                bins.setdefault((ty,key),[]).append(e)
print('marge mini du bras bas avant la fin de l echantillon (ticks de 15 ns) | n | erreur med / p90 | > 40 pts | butee')
for (ty,m) in sorted(bins):
    v=sorted(bins[(ty,m)]); n=len(v)
    if m>900: continue
    print('%s %5s n=%4d med %4d p90 %4d  >40: %5.1f %%  butee: %5.1f %%'%(ty,'ouvert' if m<0 else '%d-%d'%(m,m+49),n,v[n//2],v[int(n*.9)],100*sum(x>40 for x in v)/n,100*sum(x==999 for x in v)/n))
