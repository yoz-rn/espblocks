# bjir

rm ../secrets/passwd-esp
rm ../secrets/passwd-backend

echo "This one is for the esp01" >> ../secrets/passwd-esp
openssl rand -base64 18 >> ../secrets/passwd-esp

echo >> passwd
echo "This one is for the backend" >> ../secrets/passwd-backend
openssl rand -base64 18 >> ../secrets/passwd-backend

