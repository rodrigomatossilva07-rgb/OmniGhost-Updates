import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'


export default defineConfig({
  plugins: [react()],
  build: { outDir: '../webradar', emptyOutDir: true, assetsDir: 'assets' },
  server: {
    host: true,
    port: 5173,
    allowedHosts: true
  }
})
