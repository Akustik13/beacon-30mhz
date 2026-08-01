import 'package:flutter/material.dart';
import '../app_theme.dart';

class AboutScreen extends StatelessWidget {
  const AboutScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('About')),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            // App identity
            const Icon(Icons.radio, size: 72, color: AppColors.primary),
            const SizedBox(height: 16),
            Text('Beacon Manager',
                style: Theme.of(context).textTheme.titleLarge?.copyWith(fontSize: 24)),
            const SizedBox(height: 4),
            const Text('Version 2.0.0',
                style: TextStyle(color: AppColors.textSub, fontSize: 14)),
            const SizedBox(height: 32),
            const Divider(),

            // Company
            const SizedBox(height: 16),
            const Text('Sevskiy GmbH',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: AppColors.text)),
            const SizedBox(height: 8),
            const Text('Schatzbogen 43\n81829 Munich, Germany',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 14, color: AppColors.textSub)),
            const SizedBox(height: 10),
            _link(Icons.language, 'www.sevskiy.com'),
            const SizedBox(height: 20),
            const Divider(),

            // Author
            const SizedBox(height: 16),
            const Text('Development',
                style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: AppColors.text)),
            const SizedBox(height: 10),
            const Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.person_outline, size: 16, color: AppColors.textSub),
                SizedBox(width: 6),
                Text('Viacheslav Pryimak',
                    style: TextStyle(fontSize: 14, color: AppColors.text)),
              ],
            ),
            const SizedBox(height: 6),
            _link(Icons.email_outlined, 'prym.via@gmail.com'),
            const SizedBox(height: 4),
            _link(Icons.business_center_outlined, 'viacheslav.pryimak@sevskiy.de'),
            const SizedBox(height: 20),
            const Divider(),

            // Description
            const SizedBox(height: 16),
            const Text('About this app',
                style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: AppColors.text)),
            const SizedBox(height: 8),
            const Text(
              'Beacon Manager is a field management tool for 30 MHz radio beacon implants '
              'used in wildlife tracking research. The app communicates with STM32WB1M-based '
              'transmitters via Bluetooth Low Energy to configure schedules, monitor battery '
              'and temperature, log GPS-tagged sessions, and read accelerometer data '
              '(ISM330DHCX on eval board / LIS2DW12 on final hardware).',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 13, color: AppColors.textSub, height: 1.5),
            ),
            const SizedBox(height: 20),
            const Divider(),

            // Data & Server
            const SizedBox(height: 16),
            const Text('Data & Connectivity',
                style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: AppColors.text)),
            const SizedBox(height: 8),
            const Text(
              'Session data (GPS coordinates, temperature, battery) is stored locally on this device. '
              'Server upload of geolocation and sensor data is planned for a future version.',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 13, color: AppColors.textSub, height: 1.5),
            ),
            const SizedBox(height: 16),

            // Future features (stubs)
            _featureCard(
              icon: Icons.system_update_outlined,
              title: 'Beacon Firmware Update',
              description: 'OTA firmware update for beacon via BLE — coming soon',
              available: false,
            ),
            const SizedBox(height: 8),
            _featureCard(
              icon: Icons.download_for_offline_outlined,
              title: 'App Update from Server',
              description: 'Check and install app updates from Sevskiy server — coming soon',
              available: false,
            ),
            const SizedBox(height: 8),
            _featureCard(
              icon: Icons.cloud_upload_outlined,
              title: 'Cloud Sync',
              description: 'Upload session logs and GPS tracks to server — planned',
              available: false,
            ),
            const SizedBox(height: 20),
            const Divider(),

            // Privacy
            const SizedBox(height: 16),
            const Text('Privacy',
                style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: AppColors.text)),
            const SizedBox(height: 8),
            const Text(
              'All location and session data is currently stored locally on this device only. '
              'No data is transmitted to any server or third party in this version.',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 13, color: AppColors.textSub, height: 1.5),
            ),
            const SizedBox(height: 32),
          ],
        ),
      ),
    );
  }

  Widget _link(IconData icon, String text) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [
      Icon(icon, size: 14, color: AppColors.primary),
      const SizedBox(width: 6),
      Text(text, style: const TextStyle(fontSize: 13, color: AppColors.primary)),
    ]),
  );

  Widget _featureCard({
    required IconData icon,
    required String title,
    required String description,
    required bool available,
  }) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppColors.card,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AppColors.divider),
      ),
      child: Row(children: [
        Icon(icon, size: 28, color: available ? AppColors.primary : AppColors.textSub),
        const SizedBox(width: 12),
        Expanded(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Row(children: [
              Expanded(child: Text(title,
                  style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600))),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                decoration: BoxDecoration(
                  color: available
                      ? AppColors.secondary.withOpacity(0.2)
                      : AppColors.textSub.withOpacity(0.15),
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  available ? 'Available' : 'Coming soon',
                  style: TextStyle(
                    fontSize: 9,
                    color: available ? AppColors.secondary : AppColors.textSub,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
            ]),
            const SizedBox(height: 2),
            Text(description,
                style: const TextStyle(fontSize: 11, color: AppColors.textSub)),
          ]),
        ),
      ]),
    );
  }
}
