# SPDX-License-Identifier: GPL-2.0-or-later
"""access.py: Cloudflare Access's login in front of the dashboard.

Cloudflare Access signs every request it lets through with a JWT in the
Cf-Access-Jwt-Assertion header (RS256, the team's keys at
https://<team>.cloudflareaccess.com/cdn-cgi/access/certs). The server's own
address is public, so a request can skip Cloudflare: the dashboard trusts
only a token whose signature, issuer, audience (the Access application's AUD
tag) and times check out. Standard library only: an RSA signature check is
one pow() and a compare (RFC 8017, RSASSA-PKCS1-v1_5 with SHA-256).
"""
import base64
import hashlib
import hmac
import json
import threading
import time
import urllib.request

# DER of a SHA-256 DigestInfo, before the hash (RFC 8017, 9.2 note 1).
SHA256_INFO = bytes.fromhex('3031300d060960864801650304020105000420')
REFETCH_AFTER = 3600	# seconds: the keys rotate every few weeks
UNKNOWN_KID_WAIT = 60	# an unknown key fetches the list again at most this often


class Denied(Exception):
	pass


def b64url(text):
	return base64.urlsafe_b64decode(text + '=' * (-len(text) % 4))


def team_issuer(team):
	"""'itsneufox' or 'itsneufox.cloudflareaccess.com' (with or without
	https://): the token's issuer."""
	team = team.strip().rstrip('/')
	team = team.split('://', 1)[-1]
	if not team.endswith('.cloudflareaccess.com'):
		team += '.cloudflareaccess.com'
	return 'https://' + team


class AccessVerifier:
	def __init__(self, team, aud, fetch=None, leeway=60):
		self.issuer = team_issuer(team)
		self.aud = aud.strip()
		self.fetch = fetch or self.fetch_certs
		self.leeway = leeway
		self.keys = {}
		self.fetched = 0.0
		self.lock = threading.Lock()

	def fetch_certs(self):
		with urllib.request.urlopen(self.issuer + '/cdn-cgi/access/certs', timeout=10) as r:
			return json.load(r)

	def key(self, kid):
		with self.lock:
			age = time.monotonic() - self.fetched
			if age > REFETCH_AFTER or (kid not in self.keys and age > UNKNOWN_KID_WAIT):
				try:
					data = self.fetch()
				except Exception:	# Cloudflare unreachable: keep the keys there are
					data = None
				self.fetched = time.monotonic()
				if data is not None:
					keys = {}
					for k in data.get('keys', []):
						if k.get('kty') == 'RSA' and k.get('kid') and k.get('n') and k.get('e'):
							keys[k['kid']] = (int.from_bytes(b64url(k['n']), 'big'),
							                  int.from_bytes(b64url(k['e']), 'big'))
					if keys:
						self.keys = keys
			return self.keys.get(kid)

	def verify(self, token):
		"""The token's claims (email among them), or Denied."""
		try:
			head, body, sig = token.split('.')
			header = json.loads(b64url(head))
			claims = json.loads(b64url(body))
			signature = b64url(sig)
		except (ValueError, TypeError):
			raise Denied('malformed token')
		if not isinstance(header, dict) or header.get('alg') != 'RS256':
			raise Denied('not RS256')
		key = self.key(str(header.get('kid', '')))
		if key is None:
			raise Denied('unknown key')
		n, e = key
		size = (n.bit_length() + 7) // 8
		value = int.from_bytes(signature, 'big')
		digest = SHA256_INFO + hashlib.sha256((head + '.' + body).encode('ascii')).digest()
		if len(signature) != size or value >= n or size < len(digest) + 11:
			raise Denied('bad signature')
		expected = b'\x00\x01' + b'\xff' * (size - len(digest) - 3) + b'\x00' + digest
		if not hmac.compare_digest(pow(value, e, n).to_bytes(size, 'big'), expected):
			raise Denied('bad signature')
		if not isinstance(claims, dict) or claims.get('iss') != self.issuer:
			raise Denied('wrong issuer')
		aud = claims.get('aud')
		if self.aud not in (aud if isinstance(aud, list) else [aud]):
			raise Denied('wrong audience')
		t = time.time()
		exp, nbf = claims.get('exp'), claims.get('nbf', claims.get('iat', 0))
		if not isinstance(exp, (int, float)) or exp < t - self.leeway:
			raise Denied('expired')
		if not isinstance(nbf, (int, float)) or nbf > t + self.leeway:
			raise Denied('not yet valid')
		return claims
