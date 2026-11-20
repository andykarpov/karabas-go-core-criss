import sys; sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from lib import *
ins,order=load()
for arg in sys.argv[1:]:
    a,_,n=arg.partition(':')
    print(dump(ins,order,int(a,16),int(n) if n else 60)); print('----')
