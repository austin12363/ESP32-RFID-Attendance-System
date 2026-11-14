package com.slsu.attendance.data.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.util.Log
import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

object NetworkClient {
    private const val TAG = "NetworkClient"
    var currentBaseUrl: String = "http://192.168.4.1/"
        private set

    private var okHttpClient: OkHttpClient = buildOkHttpClient()
    private var retrofit: Retrofit = buildRetrofit()
    var apiService: Esp32ApiService = retrofit.create(Esp32ApiService::class.java)
        private set

    private fun buildOkHttpClient(): OkHttpClient {
        val logging = HttpLoggingInterceptor().apply {
            level = HttpLoggingInterceptor.Level.BASIC
        }
        return OkHttpClient.Builder()
            .connectTimeout(5, TimeUnit.SECONDS)
            .readTimeout(8, TimeUnit.SECONDS)
            .writeTimeout(5, TimeUnit.SECONDS)
            .addInterceptor(logging)
            .retryOnConnectionFailure(true)
            .build()
    }

    private fun buildRetrofit(): Retrofit {
        return Retrofit.Builder()
            .baseUrl(currentBaseUrl)
            .client(okHttpClient)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
    }

    fun updateBaseUrl(newHost: String) {
        val formatted = if (newHost.startsWith("http://") || newHost.startsWith("https://")) {
            if (newHost.endsWith("/")) newHost else "$newHost/"
        } else {
            "http://$newHost/"
        }
        currentBaseUrl = formatted
        retrofit = buildRetrofit()
        apiService = retrofit.create(Esp32ApiService::class.java)
    }

    /**
     * Forces Android to route all HTTP network requests through the Wi-Fi connection,
     * even when the ESP32 Access Point has no internet connectivity.
     */
    fun bindToWifiHotspot(context: Context) {
        val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()

        connectivityManager.registerNetworkCallback(request, object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                super.onAvailable(network)
                Log.d(TAG, "Wi-Fi Network available. Binding process to Wi-Fi.")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    connectivityManager.bindProcessToNetwork(network)
                } else {
                    @Suppress("DEPRECATION")
                    ConnectivityManager.setProcessDefaultNetwork(network)
                }
            }

            override fun onLost(network: Network) {
                super.onLost(network)
                Log.d(TAG, "Wi-Fi Network lost.")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    connectivityManager.bindProcessToNetwork(null)
                }
            }
        })
    }
}
