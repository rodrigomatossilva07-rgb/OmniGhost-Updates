FROM mcr.microsoft.com/dotnet/sdk:8.0 AS build
WORKDIR /src
COPY server/OmniGhost.AdminApi/OmniGhost.AdminApi.csproj server/OmniGhost.AdminApi/
RUN dotnet restore server/OmniGhost.AdminApi/OmniGhost.AdminApi.csproj
COPY server/OmniGhost.AdminApi/ server/OmniGhost.AdminApi/
RUN dotnet publish server/OmniGhost.AdminApi/OmniGhost.AdminApi.csproj -c Release -o /out --no-restore

FROM mcr.microsoft.com/dotnet/aspnet:8.0 AS runtime
WORKDIR /app
COPY --from=build /out .
RUN mkdir -p /app/data/audit && chown -R app:app /app
USER app
ENV ASPNETCORE_URLS=http://+:8080
EXPOSE 8080
ENTRYPOINT ["dotnet", "OmniGhost.AdminApi.dll"]
