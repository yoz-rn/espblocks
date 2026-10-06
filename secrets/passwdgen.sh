# bjir

rm passwd-esp
rm passwd-backend

echo "This one is for the esp01" >> passwd-esp
openssl rand -base64 18 >> passwd-esp

echo >> passwd
echo "This one is for the backend" >> passwd-backend
openssl rand -base64 18 >> passwd-backend

