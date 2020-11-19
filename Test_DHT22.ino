#include "DHT.h"   	// Librairie des capteurs DHT
#define DHTPIN D4    // Changer le pin sur lequel est branché le DHT

// Type de capteur : DHT11, DHT22, ....
#define DHTTYPE DHT22       // DHT 22  (AM2302)
DHT dht(DHTPIN, DHTTYPE); 

void setup() {
  Serial.begin(115200); 
  Serial.println("DHT22 initialisation.");
 
  dht.begin();
}

void loop() {
  // Lecture du taux d'humidité
  float h = dht.readHumidity();
  // Lecture de la température en Celcius
  float t = dht.readTemperature();
  // Pour lire la température en Fahrenheit
  float f = dht.readTemperature(true);
  
  // Stop le programme et renvoie un message d'erreur si le capteur ne renvoie aucune mesure
  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println("Echec de lecture !");
    return;
  }

  // Calcul la température ressentie. le calcul s'effectue à partir de la température en Fahrenheit
  float hi = dht.computeHeatIndex(f, h);
  // float ti = convertFtoC(dht.computeHeatIndex(convertCtoF(t), h));
  
  // Commande bibliothéque 
  /*
	convertCtoF(float) : converti la température Celcius en Fahrenheit
	convertFtoC(float) : converti la température Fahrenheit en Celcius
  */

  Serial.print("Humidite: "); 
  Serial.print(h);
  Serial.print("%\t");
  Serial.print("Temperature: "); 
  Serial.print(t);
  Serial.print("°C\t");
  Serial.print("Temperature ressentie: ");
  Serial.print(dht.convertFtoC(hi));
  Serial.println("°C");
  
  // Délai de 5 secondes entre chaque mesure. 
  delay(5000);
}