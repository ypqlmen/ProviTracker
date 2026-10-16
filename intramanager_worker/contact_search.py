"""Search public company contact cards. No people directories or private numbers."""
import base64
import http.client
from urllib.error import HTTPError
from concurrent.futures import ThreadPoolExecutor
from html.parser import HTMLParser
import ipaddress
import re
import socket
from urllib import request, parse, robotparser

AGENT = 'ProviTrackerBusinessContact/1.0'
BLOCKED = ('krak.dk','degulesider.dk','118.dk','180.dk','192.com','facebook.com','instagram.com','linkedin.com','whitepages.com','truecaller.com')

def public_url(url):
    p = parse.urlsplit(url)
    if p.scheme != 'https' or not p.hostname or p.username or p.password or p.port not in (None,443):
        raise ValueError('Kun offentlige HTTPS-virksomhedssider understøttes.')
    host=p.hostname.lower()
    if host=='localhost' or host.endswith(('.local','.internal')) or any(host==x or host.endswith('.'+x) for x in BLOCKED):
        raise ValueError('Private personregistre og lokale adresser understøttes ikke.')
    addresses=socket.getaddrinfo(host,443,type=socket.SOCK_STREAM)
    if not addresses or any(not ipaddress.ip_address(a[4][0]).is_global for a in addresses):
        raise ValueError('Adressen er ikke en offentlig virksomhedsside.')
    return url

class PublicHTTPSConnection(http.client.HTTPSConnection):
    def connect(self):
        public_url('https://'+self.host+'/')
        addresses=socket.getaddrinfo(self.host,443,type=socket.SOCK_STREAM)
        if any(not ipaddress.ip_address(a[4][0]).is_global for a in addresses): raise ValueError('Ikke-offentlig adresse.')
        address=addresses[0][4][0]
        # Pin the validated public address while retaining TLS hostname checks.
        self._create_connection=lambda endpoint,timeout,source_address: socket.create_connection((address,endpoint[1]),timeout,source_address)
        super().connect()

class PublicHTTPSHandler(request.HTTPSHandler):
    def https_open(self, req):
        return self.do_open(PublicHTTPSConnection,req,context=self._context)

class Redirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        public_url(newurl)
        return super().redirect_request(req,fp,code,msg,headers,newurl)

def fetch(url):
    public_url(url)
    opener=request.build_opener(request.ProxyHandler({}),Redirect(),PublicHTTPSHandler())
    with opener.open(request.Request(url,headers={'User-Agent':AGENT,'Accept':'text/html,text/plain'}),timeout=8) as response:
        if response.headers.get_content_type() not in ('text/html','text/plain'):
            raise ValueError('Siden er ikke en kontakt- eller tekstside.')
        raw=response.read(1000001)
        if len(raw)>1000000: raise ValueError('Siden er for stor.')
        return raw.decode(response.headers.get_content_charset() or 'utf-8',errors='replace')

class Links(HTMLParser):
    def __init__(self): super().__init__();self.links=[]
    def handle_starttag(self,tag,attrs):
        if tag!='a': return
        href=dict(attrs).get('href','')
        if href.startswith('//'): href='https:'+href
        if href.startswith('https://www.bing.com/ck/a'):
            value=parse.parse_qs(parse.urlsplit(href).query).get('u',[''])[0]
            if value.startswith('a1'):
                try: href=base64.urlsafe_b64decode(value[2:]+'===').decode()
                except (ValueError,UnicodeError): return
        if href.startswith('https://'): self.links.append(href)

class Cards(HTMLParser):
    def __init__(self): super().__init__();self.stack=[];self.cards=[];self.ignore=0
    def handle_starttag(self,tag,attrs):
        attrs=dict(attrs)
        if tag in ('script','style','noscript'): self.ignore+=1
        if tag in ('article','section','li','div','td'):
            self.stack.append({'tag':tag,'text':[],'phones':[]})
        href=attrs.get('href','')
        if tag=='a' and href.startswith('tel:'):
            phone=re.sub(r'[^+0-9]','',parse.unquote(href[4:]))
            if re.fullmatch(r'(?:\+45|0045)?[2-9]\d{7}',phone):
                for card in self.stack: card['phones'].append('+45 '+phone[-8:])
    def handle_data(self,text):
        if not self.ignore:
            for card in self.stack: card['text'].append(text.strip())
    def handle_endtag(self,tag):
        if tag in ('script','style','noscript') and self.ignore: self.ignore-=1
        if self.stack and self.stack[-1]['tag']==tag:
            self.cards.append(self.stack.pop())

def company_cards(html, full_name, url):
    parser=Cards();parser.feed(html)
    name=' '.join(full_name.casefold().split())
    matches=[]
    for card in parser.cards:
        text=' '.join(' '.join(card['text']).split())
        lower=text.casefold()
        # Require one personal number in a small work-related contact card.
        # A company switchboard, many-person page or residential listing is not
        # evidence that a telephone belongs to the named employee.
        phones=list(dict.fromkeys(card['phones']))
        if not re.search(r'(?<!\w)'+re.escape(name)+r'(?!\w)',lower) or len(text)>650 or len(phones)!=1: continue
        if not re.search(r'\b(konsulent|direktør|rådgiver|partner|chef|manager|salg|medarbejder|konsultant|consultant|director|ceo|cfo|team|kontaktperson)\b',lower): continue
        if re.search(r'\b(privat|privatadresse|bopæl|residential|home phone)\b',lower): continue
        if not re.search(r'\b(direkte|mobil|mobile|direct)\b',lower): continue
        matches.append({'name':full_name,'phone':phones[0],'source':url,'context':text[:500],'verification':'Muligt match · kontrollér navn og virksomhed'})
    return matches

def allowed_company_page(url, reader):
    public_url(url)
    origin=parse.urlsplit(url)
    robots=robotparser.RobotFileParser()
    try: rules=reader(origin.scheme+'://'+origin.netloc+'/robots.txt')
    except HTTPError as error:
        if error.code not in (404,410): raise ValueError('Virksomhedssiden kunne ikke kontrolleres.') from error
        rules=''
    robots.parse(rules.splitlines())
    if not robots.can_fetch(AGENT,url): raise ValueError('Virksomhedssiden tillader ikke automatisk læsning her.')
    return reader(url)

def search(payload, reader=fetch):
    name=' '.join(str(payload.get('fullName','')).split())
    company=' '.join(str(payload.get('company','')).split())
    if len(name.split())<2 or len(name)>100 or len(company)>140:
        raise ValueError('Indtast et fuldt navn med mindst for- og efternavn.')
    site=str(payload.get('website','')).strip()
    if site:
        url=site if site.startswith('https://') else 'https://'+site
        public_url(url);urls=[url]
        links=Links();links.feed(allowed_company_page(url,reader))
        host=parse.urlsplit(url).hostname
        urls.extend(u for u in links.links if parse.urlsplit(u).hostname==host and re.search(r'kontakt|contact|team|medarbejder|people',u,re.I))
    else:
        query='"'+name+'" '+company+' direkte mobil kontakt medarbejder -site:krak.dk -site:degulesider.dk -site:118.dk'
        links=Links();links.feed(reader('https://www.bing.com/search?'+parse.urlencode({'q':query})))
        urls=[u for u in links.links if parse.urlsplit(u).hostname not in ('www.bing.com','bing.com','go.microsoft.com','www.microsoft.com')]
    urls=list(dict.fromkeys(u.split('#')[0] for u in urls))[:6]
    if not urls: return {'success':False,'error':'Websøgningen gav ikke læsbare virksomhedssider. Prøv med virksomhedens hjemmeside; søgetjenesten kan have blokeret automatisk adgang.'}
    def read_company(url):
        try:
            return company_cards(allowed_company_page(url,reader),name,url)
        except Exception: return []
    found=[]
    with ThreadPoolExecutor(max_workers=2) as pool:
        for rows in pool.map(read_company,urls): found.extend(rows)
    unique={(r['phone'],r['source']):r for r in found}
    return {'success':True,'results':list(unique.values()),'message':'Kun offentlige erhvervskontakter. Kontrollér altid kilden og personens identitet.' if unique else 'Ingen direkte erhvervskontakt fundet. Prøv at tilføje virksomhed eller virksomhedens hjemmeside. Private personregistre gennemsøges ikke.'}
