import socket
import unittest
from unittest.mock import patch
from intramanager_worker.contact_search import company_cards,public_url,search

class ContactSearchTests(unittest.TestCase):
    def test_only_direct_company_contact_card(self):
        card='<article><h2>Sofie Eksempel</h2><p>Salg · rådgiver</p><p>Direkte mobil: <a href="tel:+4570701234">70 70 12 34</a></p></article>'
        result=company_cards(card,'Sofie Eksempel','https://firma.example/team')
        self.assertEqual(len(result),1)
        self.assertEqual(result[0]['phone'],'+45 70701234')
        self.assertEqual(result[0]['source'],'https://firma.example/team')
        for invalid in [card.replace('Direkte mobil','Privat mobil'),card.replace('Salg · rådgiver','Privatperson'),card.replace('Sofie Eksempel','Anden Person'),card.replace('Sofie Eksempel','Sofie Eksempelnavn'),card.replace('</p></article>','<a href="tel:+4570705678">Andet nummer</a></p></article>')]:
            self.assertEqual(company_cards(invalid,'Sofie Eksempel','https://firma.example/team'),[])

    def test_no_local_network_or_people_directories(self):
        for url in ['http://firma.example','file:///tmp/example','https://localhost','https://krak.dk/person','https://118.dk','https://firma.example:8443','https://u:p@firma.example']:
            with self.assertRaises(ValueError):public_url(url)
        with patch.object(socket,'getaddrinfo',return_value=[(2,1,6,'',('127.0.0.1',443))]):
            with self.assertRaises(ValueError):public_url('https://firma.example')

    def test_disallowed_company_page_is_not_read(self):
        visits=[]
        def reader(url):
            visits.append(url)
            if url.endswith('/robots.txt'): return 'User-agent: *\nDisallow: /'
            self.fail('A disallowed company page must not be read')
        with patch('intramanager_worker.contact_search.public_url',side_effect=lambda url:url):
            with self.assertRaises(ValueError): search({'fullName':'Sofie Eksempel','website':'https://firma.example'},reader)
        self.assertEqual(visits,['https://firma.example/robots.txt'])

if __name__=='__main__':unittest.main()
